"""Adapt the pinned Windows generator's output for the PS5 ELF/LP64 ABI.

Input is immutable. Adapted sources go to a separate ignored build tree.
Only fully generated input is accepted; no substitute data-area headers.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


# Recognise literals/comments before identifiers so wording, paths and escape
# sequences cannot be changed by the type conversion.
TOKENS = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|[uUL])?"(?:\\.|[^"\\])*"'
    r"|(?:u8|[uUL])?'(?:\\.|[^'\\])*'"
    r'|\b[A-Za-z_]\w*\b|\b(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\b'
)
ALLOCATE = re.compile(r'__declspec\(allocate\("(\.khdi\$m|\.khfr\$m)"\)\)')
NITRO_INLINE_HELPERS = (
    "NNS_G2dFontGetBpp", "NNS_G2dFontGetCellHeight", "NNS_G2dFontGetCellWidth",
    "NNS_G3dGetMat", "NNS_G3dGetMatIdxByName", "NNS_G3dGetNodeInfo",
    "NNS_G3dGetResDataByIdx", "NNS_G3dGetResNameByIdx",
)
NITRO_INLINE = re.compile(
    r"(?m)^([ \t]*)inline\s+(u8|int|khdays_ref)\s+(" +
    "|".join(map(re.escape, NITRO_INLINE_HELPERS)) + r")\s*\("
)


def patch_nitro_inline_helpers(source: str) -> str:
    # These SDK helpers are fully defined in each generated translation unit.
    # MSVC emits a body for a plain inline; C99 deliberately does not. Give the
    # local definitions internal linkage so a non-inlined call has its actual
    # body at every optimization level, without a second reference ABI shim.
    return NITRO_INLINE.sub(r"\1static inline \2 \3(", source)


def adapt_source(source: str) -> str:
    # The generator preserves MSVC's 32-bit long spelling. FreeBSD is LP64.
    # Generated units contain no system includes; host pointer-sized types are
    # already emitted as 64-bit integers by the MSVC preprocessor.
    tokens = list(TOKENS.finditer(source))
    changes: list[tuple[int, int, str]] = []
    words = {i: token.group() for i, token in enumerate(tokens)}
    consumed: set[int] = set()
    for i, token in enumerate(tokens):
        if re.fullmatch(r'(?:0[xX][0-9a-fA-F]+|[0-9]+)(?:[uU]?[lL]|[lL][uU]?)', token.group()):
            changes.append((token.start(), token.end(), re.sub('[lL]', '', token.group())))
            continue
        if i in consumed or token.group() != "long":
            continue
        # Whitespace (including comments) can separate a type's words.
        j = i + 1
        while j < len(tokens) and words[j].startswith(("/*", "//")):
            j += 1
        between = source[token.end():tokens[j].start()] if j < len(tokens) else ""
        adjacent = j < len(tokens) and not re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|\s+', '', between)
        following = words.get(j) if adjacent else None
        if following == "long":
            consumed.add(j)  # long long remains 64-bit
        elif following in ("double", "int"):
            # MSVC long double is double; long int becomes int.
            changes.append((token.start(), token.end(), ""))
        else:
            changes.append((token.start(), token.end(), "int"))
    for start, end, value in reversed(changes):
        source = source[:start] + value + source[end:]
    source = ALLOCATE.sub(
        lambda m: '__attribute__((section("' +
        ("khdays_data" if m[1] == ".khdi$m" else "khdays_functions") +
        '"), aligned(8), used))', source)
    source = re.sub(r'^\s*#pragma section\([^\n]*\)\s*$', '', source, flags=re.M)
    source = re.sub(r'#pragma optimize\("",\s*off\)', '#pragma clang optimize off', source)
    source = re.sub(r'#pragma optimize\("",\s*on\)', '#pragma clang optimize on', source)
    return source


def patch_generated_prototypes(relative: Path, source: str) -> str:
    """Restore declarations that the DS C build supplied through a shared TU.

    The decompilation keeps the NitroSDK sources as independent translation
    units.  The original ARM build tolerated this implicit declaration, while
    Clang's C99 mode rejects it.  Keep the correction in the generated-to-ELF
    boundary so the pinned upstream tree remains untouched.
    """
    if relative.as_posix().startswith("rewritten/libs/nns/"):
        source = patch_nitro_inline_helpers(source)
    if relative.as_posix().endswith("libs/nitro/fs/calls/FSi_SendCommand.c"):
        marker = "extern void FSi_ReleaseCommand(khdays_ref p_file, FSResult ret);"
        declaration = "extern BOOL FSi_ExecuteSyncCommand(khdays_ref p_file);\n"
        if declaration not in source and marker in source:
            source = source.replace(marker, declaration + marker, 1)
    if relative.as_posix().endswith((
        "libs/nns/g2d/auto/NNS_G2dInitImageProxy.c",
        "libs/nns/g2d/auto/NNS_G2dInitImagePaletteProxy.c",
    )):
        # These generated units contain both the old SDK extern and their
        # local static helper. Clang correctly rejects the conflicting
        # linkage, while the original ARM compiler accepted it.
        source = source.replace(
            "extern void InitializeVRamLocation_(khdays_ref pVramLocation);\n", "", 1
        )
    # Some NitroSDK units repeat this pattern with local helpers. Remove an
    # extern declaration whenever the same translation unit defines that name
    # with static linkage. #line markers may occur between the two statements.
    static_names = set(re.findall(
        r"\b(?:inline\s+)?static\s+[^;\n{}]*?\b([A-Za-z_]\w*)\s*\(", source
    ))
    for name in static_names:
        source = re.sub(
            rf"(?m)^[ \t]*extern\b[^;\n]*\b{re.escape(name)}\s*\([^;\n]*\);\r?\n",
            "",
            source,
        )
    if relative.as_posix().endswith("libs/nns/snd/calls/NNS_SndArcReadFile.c"):
        marker = '#include "khdays_references.h"\n'
        declaration = "extern void FSi_WaitForCardThread(int unused);\n"
        if declaration not in source and marker in source:
            source = source.replace(marker, marker + declaration, 1)
    if relative.as_posix().endswith("libs/nns/snd/calls/NNSi_SndArcStrm_MakeWaveData.c"):
        # The DS source uses `*((AdpcmState *)srcp)++`.  After reference
        # rewriting that becomes an increment applied to a cast, which is
        # rejected by Clang. Express the same copy and pointer advance in two
        # statements.
        lines = source.splitlines(keepends=True)
        fixed: list[str] = []
        for line in lines:
            if "struct AdpcmState" in line and "srcp" in line and "+= sizeof(struct AdpcmState)" in line:
                indent = line[: len(line) - len(line.lstrip())]
                fixed.append(indent + "(*(struct AdpcmState *)KHDAYS_W(state) = *(struct AdpcmState *)KHDAYS_W((khdays_ref)(srcp)));\n")
                fixed.append(indent + "srcp += sizeof(struct AdpcmState);\n")
            else:
                fixed.append(line)
        source = "".join(fixed)
    if relative.as_posix().endswith("libs/msl/c/auto/strcpy.c"):
        # The MSL routine advances typed pointers through casts.  Reference
        # rewriting turns those increments into assignments to a cast, which
        # is not an lvalue in standard C.
        replacements = {
            "((((khdays_ref)(destb))) -= sizeof(int));": "(destb -= sizeof(int));",
            "((*(int *)KHDAYS_W(((((khdays_ref)(destb))) += sizeof(int)))) = w);":
                "((*(int *)KHDAYS_W((destb += sizeof(int)))) = w);",
            "(w = (*(int *)KHDAYS_W(((((khdays_ref)(fromb))) += sizeof(int)))));":
                "(w = (*(int *)KHDAYS_W((fromb += sizeof(int)))));",
            "((((khdays_ref)(destb))) += sizeof(int));": "(destb += sizeof(int));",
        }
        for before, after in replacements.items():
            source = source.replace(before, after)
    if relative.as_posix().endswith("libs/msl/c/auto/strcmp.c"):
        # As in strcpy, the original decomp increments a reference wrapped in
        # a cast; after reference rewriting the cast is not assignable.
        replacements = {
            "(l1 = (*(int *)KHDAYS_W(((((khdays_ref)(left))) += sizeof(int)))));":
                "(l1 = (*(int *)KHDAYS_W((left += sizeof(int)))));",
            "(r1 = (*(int *)KHDAYS_W(((((khdays_ref)(right))) += sizeof(int)))));":
                "(r1 = (*(int *)KHDAYS_W((right += sizeof(int)))));",
        }
        for before, after in replacements.items():
            source = source.replace(before, after)
    if relative.as_posix().endswith(
        "src/overlays/scenes/ov002_field/Ov002_ResolveCameraAgainstCollision.c"
    ):
        # The decomp has an early bare return in a function returning int;
        # the initialized result is the intended value on that path.
        source = source.replace("    return;\n", "    return nResult;\n", 1)
    if relative.as_posix().endswith(
        "src/overlays/scenes/ov002_field/Ov002_SendTallyUpdate.c"
    ):
        source = source.replace("    return;\n", "    return 0;\n", 1)
    if relative.as_posix().endswith(
        "src/overlays/scenes/ov005_mission_result/Ov005_FadeInResultScreen.c"
    ):
        marker = '#include "khdays_references.h"\n'
        declarations = (
            "extern void Ov005_ConfigureResultTweens(void);\n"
            "extern void Tween_Start(khdays_ref);\n"
        )
        if declarations not in source and marker in source:
            source = source.replace(marker, marker + declarations, 1)
    if relative.as_posix().endswith(
        "src/overlays/scenes/ov008_camp_menu/Ov008_MainMenu_SetupDisplay.c"
    ):
        marker = '#include "khdays_references.h"\n'
        declaration = "extern void GX_SetBankForOBJExtPltt(int mask);\n"
        if declaration not in source and marker in source:
            source = source.replace(marker, marker + declaration, 1)
    if relative.as_posix().endswith(
        "src/overlays/screens/ov025_camp_menu_2/Ov025_MainMenu_SetupDisplay.c"
    ):
        marker = '#include "khdays_references.h"\n'
        declaration = "extern void GX_SetBankForOBJExtPltt(int mask);\n"
        if declaration not in source and marker in source:
            source = source.replace(marker, marker + declaration, 1)
    if relative.as_posix().endswith(
        "src/overlays/scenes/ov012_opening/Ov012_MobiClip_SubtitleNextChar.c"
    ):
        source = source.replace("    return;\n", "    return 0;\n", 1)
    if relative.as_posix().endswith(
        "src/overlays/system/ov024_mobiclip/Ov024_MobiClip_SubtitleNextChar.c"
    ):
        source = source.replace("    return;\n", "    return 0;\n", 1)
    return source


def write_changed(path: Path, content: str) -> None:
    if not path.exists() or path.read_text(encoding="utf-8") != content:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")


def repair_nitro_inline_tree(output: Path) -> dict:
    """Repair only affected existing SDK sources; preserve all other mtimes."""
    output = output.resolve()
    directory = output / "rewritten" / "libs" / "nns"
    if not directory.is_dir():
        raise FileNotFoundError(f"No adapted NitroSDK tree: {directory}")
    repaired = []
    for path in sorted(directory.rglob("*.c")):
        source = path.read_text(encoding="utf-8")
        if not NITRO_INLINE.search(source):
            continue
        adapted = patch_generated_prototypes(path.relative_to(output), source)
        write_changed(path, adapted)
        repaired.append(path.relative_to(output).as_posix())
    return {"output": str(output), "repaired_count": len(repaired), "repaired": repaired}


def prepare(generated: Path, output: Path) -> dict:
    generated, output = generated.resolve(), output.resolve()
    if output == generated or generated in output.parents or output in generated.parents:
        raise ValueError("ELF output must be separate from the upstream generator directory")
    for filename in ("sources.cmake", "khdays_data_area.h", "khdays_ds_symbols.h"):
        if not (generated / filename).is_file():
            raise FileNotFoundError(f"Generator has not completed: {generated / filename}")
    source_list = (generated / "sources.cmake").read_text(encoding="utf-8")
    paths = re.findall(r'"([^"\n]+\.c)"', source_list)
    if not paths:
        raise ValueError("No generated C units in sources.cmake")
    converted = []
    digest = hashlib.sha256()
    for value in paths:
        original = Path(value)
        if not original.is_absolute():
            original = generated / original
        relative = original.resolve().relative_to(generated)
        destination = output / relative
        source = original.read_text(encoding="utf-8")
        digest.update(relative.as_posix().encode())
        digest.update(source.encode())
        adapted = adapt_source(source)
        adapted = patch_generated_prototypes(relative, adapted)
        write_changed(destination, adapted)
        converted.append(destination.as_posix())
    for filename in ("khdays_data_area.h", "khdays_ds_symbols.h", "ds_functions.c"):
        write_changed(output / filename, (generated / filename).read_text(encoding="utf-8"))
    write_changed(output / "sources.cmake", "set(KHDAYS_REWRITTEN\n" +
                  "".join(f'    "{p}"\n' for p in converted) + ")\n")
    report = {"generated": str(generated), "output": str(output),
              "source_count": len(converted), "input_sha256": digest.hexdigest(),
              "game_long_bits": 32, "host_pointer_bits": 64,
              "format": "ELF", "compiled": False}
    write_changed(output / "adaptation.json", json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repair-nitro-inline", action="store_true",
                        help="repair only local Nitro inline linkage in an existing ELF tree")
    args = parser.parse_args()
    if args.repair_nitro_inline:
        result = repair_nitro_inline_tree(args.out)
    else:
        if args.generated is None:
            parser.error("--generated is required unless --repair-nitro-inline is used")
        result = prepare(args.generated, args.out)
    print(json.dumps(result, indent=2))
