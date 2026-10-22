#!/usr/bin/env python3
"""Copy/adapt the pinned native runtime for the PS5 ELF build.

The cache is read-only input. The JSON manifest inventories the C/C++ units
and the native PS5 substitutions. C++ asset units belong to the separate
runtime_assets.cmake target; this script does not fake unavailable services.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


REPLACED = {
    "runtime/core/clock.c": "native/ps5/clock_ps5.c",
    "runtime/core/data_init.c": "native/ps5/data_init_ps5.c",
    "runtime/core/diag.c": "native/ps5/diag_ps5.c + generated diag_hooks.c",
    "runtime/core/io_guard.c": "native/ps5/io_guard_ps5.c",
    "runtime/core/memory.c": "native/ps5/memory_ps5.c",
    "runtime/core/window.c": "native/ps5/window_ps5.c",
    "runtime/core/start.c": "native/ps5/start_ps5.c",
    "runtime/core/rom.cpp": "native/ps5/rom_ps5.c",
}
EXCLUDED: dict[str, str] = {}
EXTRA = ("host/net/net_posix.c", "host/screens/ui_text.c", "host/render/png_io.c")
SDL_NAMES = {
    "SDL_Mutex": "KhdaysPosixMutex",
    "SDL_CreateMutex": "khdays_posix_mutex_create",
    "SDL_LockMutex": "khdays_posix_mutex_lock",
    "SDL_UnlockMutex": "khdays_posix_mutex_unlock",
    "SDL_CreateThread": "khdays_posix_thread_create",
    "SDL_WaitThread": "khdays_posix_thread_wait",
    "SDL_GetTicksNS": "khdays_posix_ticks_ns",
    "SDL_LoadFile": "khdays_posix_load_file",
    "SDL_SaveFile": "khdays_posix_save_file",
    "SDL_strncasecmp": "strncasecmp",
    "SDL_getenv": "getenv",
    "SDL_free": "free",
    "SDLCALL": "",
}


def adapt(relative: str, source: str) -> str:
    source = source.replace("#include <windows.h>", '#include "runtime_posix.h"')
    source = source.replace("#include <SDL3/SDL.h>", '#include "runtime_posix.h"')
    source = source.replace("#include <intrin.h>", "/* compiler return-address builtin; no MSVC intrinsics */")
    source = source.replace("_ReturnAddress()", "__builtin_return_address(0)")
    source = source.replace("__declspec(noreturn)", "__attribute__((noreturn))")
    source = source.replace("_strdup(", "strdup(")
    if relative in {"runtime/core/boot.c", "runtime/core/language.c", "runtime/input/input.c"}:
        # All upstream formats in these units convert only integers (%x, %u,
        # %d and %n); there are no sscanf_s string-buffer size arguments.
        source = source.replace("sscanf_s(", "sscanf(")
    if relative == "runtime/input/input.c":
        source = source.replace("strtok_s(", "strtok_r(")
    source = re.sub(r"\bSDL_Thread\s*\*", "KhdaysPosixThread ", source)
    for old, new in SDL_NAMES.items():
        source = re.sub(rf"\b{re.escape(old)}\b", new, source)
    if relative == "hal/hal.h":
        source = re.sub(
            r"#if defined\(_WIN64\).*?#endif",
            "#include <stdint.h>\ntypedef uintptr_t khdays_uptr;",
            source,
            count=1,
            flags=re.S,
        )
    if relative == "runtime/sound/sound_mods.c":
        # Restrict conversion to the four pinned directory format literals;
        # a textual escape such as "\\\\n" must never become "/n".
        for before, after in {
            r'"mods\\*"': '"mods/*"',
            r'"mods\\%s\\sounds"': '"mods/%s/sounds"',
            r'"mods\\%s\\sounds\\wave"': '"mods/%s/sounds/wave"',
            r'"mods\\%s\\sounds\\%s"': '"mods/%s/sounds/%s"',
        }.items():
            source = source.replace(before, after)
    if relative == "runtime/sound/sound_dump.c":
        for before, after in {
            r'"%s\\.git"': '"%s/.git"',
            r'"%s\\saves"': '"%s/saves"',
            r"strrchr(at, '\\')": "strrchr(at, '/')",
            r"full[strlen(saves)] == '\\'": "full[strlen(saves)] == '/'",
            r"saves\\ (which ": "saves/ (which ",
        }.items():
            source = source.replace(before, after)
    if relative == "runtime/mods/names.cpp":
        source = source.replace(r' + "\\" + ', ' + "/" + ')
    if relative == "host/screens/ui_text.c":
        begin = source.index("int khdays_language_of_system(void)")
        end = source.index("int khdays_language_choose(int setting)", begin)
        source = source[:begin] + """int khdays_language_of_system(void)
{
    /* Respect a configured POSIX locale; unsupported locales use upstream's
     * English default. The settings/environment override still takes priority. */
    const char *variables[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (size_t i = 0; i < sizeof(variables) / sizeof(variables[0]); ++i) {
        const char *locale = getenv(variables[i]);
        if (locale != NULL && *locale != '\\0') {
            const int found = khdays_language_from_code(locale);
            return found != 0 ? found : KHDAYS_LANGUAGE_ENGLISH;
        }
    }
    return KHDAYS_LANGUAGE_ENGLISH;
}

""" + source[end:]
    if relative == "runtime/input/input.c":
        # Upstream got the CRT spelling transitively on MSVC.
        source = '#include "runtime_posix.h"\n' + source
    if relative == "runtime/core/runtime.c" and "void khdays_hal_unimplemented(" in source:
        source = '#include "diag_ps5.h"\n' + source
        source = source.replace(
            '    fprintf(stderr, "khdays-native: reached an unimplemented routine: %s\\n", what);',
            '    khdays_ps5_diag_note("unimplemented", what);\n'
            '    fprintf(stderr, "khdays-native: reached an unimplemented routine: %s\\n", what);', 1)
        source = source.replace(
            '    fprintf(stderr, "khdays-native: reached a call the ROM makes differently: %s\\n", what);',
            '    khdays_ps5_diag_note("ABI gap", what);\n'
            '    fprintf(stderr, "khdays-native: reached a call the ROM makes differently: %s\\n", what);', 1)
        source = source.replace(
            '    khdays_log_frame(frame);',
            '    khdays_ps5_diag_state(frame, scene[2]);\n    khdays_log_frame(frame);', 1)
    if relative == "ui/bg.c":
        source = "#include <stddef.h>\n" + source
    if relative == "runtime/mods/textures.c":
        # block_of is called only for nonnegative replacement IDs; +1 keeps
        # the unsigned input nonzero. unsigned long is 64-bit on ELF/LP64.
        source = source.replace("_BitScanReverse(&k, (unsigned long)id / BLOCK + 1);",
                                "k = 31u - (unsigned)__builtin_clz((unsigned)id / BLOCK + 1u);")
    if relative == "runtime/sound/sound_bridge.c":
        # uint64_t is unsigned long on ELF/LP64, unsigned long long on MSVC.
        # Match the native sound engine's callback type exactly.
        source = source.replace("static void run(void *context, u64 cycle)",
                                "static void run(void *context, uint64_t cycle)")
        source = source.replace('"strm/%ld", waiting - 1', '"strm/%d", (int)waiting - 1')
    if relative == "runtime/display/display.c":
        decision = "    return !(want & KHDAYS_WANT_2D) || host_checks()"
        if source.count(decision) != 1:
            raise ValueError("display.c no longer has the expected reference renderer decision")
        source = source.replace(
            decision,
            "    /* UI asks for 2D descriptions even when the PS5 presenter only consumes\n"
            "     * bitmaps. Only a host that actually draws 2D may omit those bitmaps. */\n"
            "    return !(khdays_host_frame_wants() & KHDAYS_WANT_2D) ||\n"
            "           !(want & KHDAYS_WANT_2D) || host_checks()",
            1,
        )
        include = '#include "../../host/app/host.h"\n'
        if include not in source:
            raise ValueError("display.c no longer has the expected host include")
        source = source.replace(
            include,
            include + "extern void khdays_ps5_video_report_render(unsigned frame, uint32_t scene, int polygons, "
                      "uint64_t began, uint64_t engine_a, uint64_t ui, uint64_t before3d, uint64_t ended);\n"
                      "extern void khdays_ps5_video_report_ds(unsigned frame, const char *stage, "
                      "int reference, int want, int ui, uint32_t scene, const uint8_t *io, "
                      "const uint8_t *palette, const uint8_t *oam, "
                      "const uint32_t *layer3d, const KhdaysVramPages *vram, "
                      "const KhdaysG3dList *list, const uint32_t *upper, "
                      "const uint32_t *lower);\n",
            1,
        )
        present = "    if (p == local) {\n        khdays_host_present(upper, lower);"
        if present not in source:
            raise ValueError("display.c no longer has the expected direct-present path")
        source = source.replace(
            present,
            "    if (p == local) {\n"
            "        khdays_ps5_video_report_ds(frame, \"composed\", reference, want, khdays_ui_any(), "
            "*(const uint32_t *)KHDAYS_W(0x0204bdb0u), in->io, in->palette, in->oam, layer3d, "
            "in->vram, khdays_g3d_shown(), upper, lower);\n"
            "        khdays_host_present(upper, lower);",
            1,
        )
        ui = "    if (khdays_ui_any()) {\n        ui_frame(p, in, reference, frame);"
        if source.count(ui) != 1:
            raise ValueError("display.c no longer has the expected UI composition boundary")
        source = source.replace(
            ui,
            "    khdays_ps5_video_report_ds(frame, \"raw2d\", reference, want, khdays_ui_any(), "
            "*(const uint32_t *)KHDAYS_W(0x0204bdb0u), in->io, in->palette, in->oam, layer3d, "
            "in->vram, khdays_g3d_shown(), p->screens[p->upper], p->screens[!p->upper]);\n" + ui, 1)
        raster = "        khdays_gpu3d_draw(khdays_g3d_shown(), khdays_io_host, khdays_vram_pages(), layer3d);\n        layer3d_list = list3d_serial;"
        if raster not in source:
            raise ValueError("display.c no longer has the expected software 3D raster path")
        source = source.replace(
            raster,
            raster + "\n        khdays_ps5_video_report_ds(frame + 1, \"3d-render\", -1, want, khdays_ui_any(), "
            "*(const uint32_t *)KHDAYS_W(0x0204bdb0u), khdays_io_host, "
            "(const uint8_t *)KHDAYS_W(0x05000000u), (const uint8_t *)KHDAYS_W(0x07000000u), "
            "layer3d, khdays_vram_pages(), "
            "khdays_g3d_shown(), NULL, NULL);",
            1,
        )
        render_next = "    draw_3d_for_next(frame, want);"
        if source.count(render_next) != 1 or source.count("    QueryPerformanceCounter(&t2);") != 1:
            raise ValueError("display.c no longer has the expected render profiling boundaries")
        source = source.replace(render_next,
            "    LARGE_INTEGER ps5_before3d;\n    QueryPerformanceCounter(&ps5_before3d);\n" + render_next, 1)
        source = source.replace("    QueryPerformanceCounter(&t2);",
            "    QueryPerformanceCounter(&t2);\n"
            "    khdays_ps5_video_report_render(frame, *(const uint32_t *)KHDAYS_W(0x0204bdb0u), "
            "khdays_g3d_shown()->npolys, (uint64_t)t0.QuadPart, (uint64_t)ta.QuadPart, "
            "(uint64_t)t1.QuadPart, (uint64_t)ps5_before3d.QuadPart, (uint64_t)t2.QuadPart);", 1)
    return source


def diag_hooks(source: str) -> str:
    """Preserve upstream update hooks; the reference lookup validates code."""
    source = source[source.index("/* KHDAYS_TRACE_SCRIPT:"):]
    source = source.replace("    extern char __ImageBase;\n", "")
    source = source.replace("    const IMAGE_NT_HEADERS *nt;\n", "")
    source = source.replace("    const u8 *base = (const u8 *)&__ImageBase;\n", "")
    ending = source.index("    nt = (const IMAGE_NT_HEADERS *)")
    source = source[:ending] + """    /* khdays_check_update has already resolved next through the game's
     * function-reference registry. A PE-image range check has no ELF meaning. */
}
"""
    return """/* Adapted from upstream diag.c (MIT, native/UPSTREAM-LICENSE).
 * Keep the runtime's input/menu/situation hooks as well as script traces. */
#include "runtime/core/runtime.h"
#include "runtime/core/events.h"
#include "runtime/core/window.h"
#include "runtime/input/input.h"
#include "runtime/menus/camp.h"
#include "runtime/menus/charselect.h"
#include "runtime/menus/cinelist.h"
#include "runtime/net/roomlist.h"
#include "ui/situation.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define symbol_name khdays_diag_function_at

""" + source


def write_changed(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != data:
        path.write_text(data, encoding="utf-8", newline="\n")


def adapt_cpu_compositor(source: str, shared: str) -> tuple[str, str]:
    """Keep shared pixel arithmetic, pass per-call state without emulated TLS.

    PS5's emutls lookup calls pthread_once/getspecific. The original performs
    these lookups inside pixel/layer/texel helpers. An explicit stack context
    preserves concurrency and reentrancy without a TLS lookup per texel. The
    original C/HLSL header is not changed; only this C instantiation uses the
    generated context header.
    """
    begin = source.index("/* Each thread's own:")
    end = source.index("static uint kd_read8(", begin)
    original_state = source[begin:end]
    if "static KD_THREAD const KhdaysGfxMemory *kd_memory;" not in original_state:
        raise ValueError("unexpected compositor TLS state")
    source = source[:begin] + "/* Per-call context, declared below. */\n" + source[end:]
    source = source.replace("static KD_THREAD uint32_t line_desc[KHDAYS_DESC_WORDS];",
                            "uint32_t line_desc[KHDAYS_DESC_WORDS];", 1)
    source = source.replace('#include "compose.h"', '#include "compose_context.h"', 1)
    helpers = set(re.findall(r"(?m)^static (?:inline )?\w+\s+(\w+)\(", source))
    helpers.update(re.findall(r"(?m)^KD_FN \w+ (\w+)\(", shared))
    if not {"use", "kd_pixel", "kd_read16"}.issubset(helpers):
        raise ValueError("unexpected compositor helpers")
    skip = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    calls = re.compile(skip + r"|\b(" + "|".join(sorted(helpers)) + r")\s*\(")
    members = re.compile(skip + r"|\b(kd_desc|kd_memory|kd_bg_area|kd_obj_area|kd_bgext_area|kd_objext_area)\b")

    def convert(text: str) -> str:
        text = calls.sub(lambda m: m[1] + "(ctx, " if m[1] else m[0], text)
        definitions = re.compile(r"(?m)^((?:static (?:inline )?|KD_FN )\w+\s+(\w+))\(ctx, ([^)]*)\)")

        def parameters(match):
            parameters = match[3].strip()
            prefix = "KhdaysComposeContext *ctx" if match[2] == "use" else "const KhdaysComposeContext *ctx"
            return match[1] + "(" + prefix + (", " + parameters if parameters not in {"void", "KD_VOID", ""} else "") + ")"

        text = definitions.sub(parameters, text)
        text = text.replace("(ctx, )", "(ctx)")
        text = members.sub(lambda m: "ctx->" + m[1] if m[1] else m[0], text)
        text = re.sub(r"((?:static (?:inline )?|KD_FN )\w+\s+\w+\([^)]*\*ctx[^)]*\)\s*\{)",
                      r"\1\n    (void)ctx;", text)
        return text

    source, shared = convert(source), convert(shared)
    context = """typedef struct KhdaysComposeContext {
    const uint32_t *kd_desc;
    const KhdaysGfxMemory *kd_memory;
    int kd_bg_area, kd_obj_area, kd_bgext_area, kd_objext_area;
} KhdaysComposeContext;
"""
    source = source.replace("/* Per-call context, declared below. */", context, 1)
    for name in ("khdays_gfx_compose_cpu_rect", "khdays_gfx_pixel_cpu", "khdays_gfx_describe_wide"):
        declaration = re.compile(r"(\b(?:void|uint32_t) " + name + r"\([^)]*\)\s*\{)")
        source, count = declaration.subn(r"\1\n    KhdaysComposeContext context = {0};\n    KhdaysComposeContext *ctx = &context;", source)
        if count != 1:
            raise ValueError(f"unexpected compositor entrypoint {name}")
    # Pure helpers accept the context too: make that explicit under -Wextra.
    for name in ("kd_asfloat", "to_rgb888"):
        source = re.sub(r"(\b" + name + r"\(const KhdaysComposeContext \*ctx, [^)]*\)\s*\{)",
                        r"\1\n    (void)ctx;", source, count=1)
    return source, shared


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", "--source", dest="upstream", type=Path, required=True,
                        help="Pinned upstream repository root")
    parser.add_argument("--output", "--out", dest="output", type=Path, required=True,
                        help="Independent generated port tree")
    args = parser.parse_args()
    upstream, output = args.upstream.resolve(), args.output.resolve()
    port = upstream / "port"
    if not (port / "runtime/core/runtime.c").is_file():
        parser.error("--upstream is not a native khdays-port tree")
    if output == port or port in output.parents or output in port.parents:
        parser.error("--output must not overlap the cached upstream port")
    sources = sorted(
        p.relative_to(port).as_posix()
        for folder in (port / "runtime", port / "ui")
        for p in folder.rglob("*.c")
        if p.relative_to(port).as_posix() not in REPLACED
        and p.relative_to(port).as_posix() not in EXCLUDED
    ) + list(EXTRA)
    cpp_sources = sorted(p.relative_to(port).as_posix()
                         for folder in (port / "runtime", port / "ui")
                         for p in folder.rglob("*.cpp"))
    manifest = {
        "upstream": upstream.as_posix(), "output": output.as_posix(),
        "sources": sources + ["diag_hooks.c"],
        "copied_cpp_sources": cpp_sources,
        "replaced": REPLACED, "excluded": EXCLUDED,
        "hal_provider": "The game archive already contains rewritten/native_hal/*.c; "
                        "raw port/hal sources must not be compiled with native x64 pointers.",
        "cpp_target": "native/runtime_assets.cmake",
        "included_cpp_sources": [name for name in cpp_sources if name not in REPLACED],
        "assets_cpp_sources": sorted(p.relative_to(upstream).as_posix()
                                     for p in (upstream / "assets/src").rglob("*.cpp")),
        "services": ["pthread mutex/condition/thread", "sequentially consistent 32-bit atomics",
                     "monotonic nanosecond clock", "POSIX directory enumeration and file I/O",
                     "POSIX fatal signals and guarded I/O faults"],
        "hardware_tested": False, "files": {},
    }
    if (port / "gfx/compose_cpu.c").is_file():
        compositor, compose_context = adapt_cpu_compositor(
            (port / "gfx/compose_cpu.c").read_text(encoding="utf-8"),
            (port / "gfx/compose.h").read_text(encoding="utf-8"))
        write_changed(output / "gfx/compose_cpu.c", compositor)
        write_changed(output / "gfx/compose_context.h", compose_context)
        manifest["cpu_compositor"] = {"source": "gfx/compose_cpu.c", "context": "per-call, no emulated TLS",
                                      "adapted_sha256": hashlib.sha256(compositor.encode()).hexdigest()}
    for path in sorted(port.rglob("*.h")):
        relative = path.relative_to(port).as_posix()
        original = path.read_text(encoding="utf-8")
        write_changed(output / relative, adapt(relative, original))
    for relative in sources:
        original = (port / relative).read_text(encoding="utf-8")
        adapted = adapt(relative, original)
        remaining_sdl = re.findall(r"\bSDL_[A-Za-z0-9_]+", adapted)
        if remaining_sdl:
            parser.error(f"unadapted SDL service in {relative}: {sorted(set(remaining_sdl))}")
        write_changed(output / relative, adapted)
        manifest["files"][relative] = {"upstream_sha256": hashlib.sha256(original.encode()).hexdigest(),
                                       "adapted_sha256": hashlib.sha256(adapted.encode()).hexdigest()}
    for relative in cpp_sources:
        original = (port / relative).read_text(encoding="utf-8")
        adapted = adapt(relative, original)
        write_changed(output / relative, adapted)
        manifest["files"][relative] = {"upstream_sha256": hashlib.sha256(original.encode()).hexdigest(),
                                       "adapted_sha256": hashlib.sha256(adapted.encode()).hexdigest()}
    write_changed(output / "diag_hooks.c", diag_hooks((port / "runtime/core/diag.c").read_text(encoding="utf-8")))
    write_changed(output / "adaptation.json", json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    write_changed(output / "sources.cmake", "set(KHDAYS_NATIVE_PS5_RUNTIME_C_SOURCES\n" +
                  "".join(f'    "${{KHDAYS_NATIVE_RUNTIME_PORT}}/{name}"\n' for name in manifest["sources"]) + ")\n")
    print(f"Prepared {len(manifest['sources'])} runtime/UI/host C sources in {output}")
    print(f"C++ auxiliaries: {len(manifest['included_cpp_sources'])}; "
          f"asset C++ sources: {len(manifest['assets_cpp_sources'])}; entrypoint: native/ps5/start_ps5.c")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
