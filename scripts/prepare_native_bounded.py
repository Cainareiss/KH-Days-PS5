"""Run the pinned generator with bounded queues, cache reuse and a rewrite checkpoint.

The upstream source and its cache keys are unchanged. This replaces only the
parent process's scheduling policy and discards text copies after they have
been written to disk, before the reference rewrite starts.
"""
from __future__ import annotations

import argparse
import gc
import hashlib
import inspect
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

_workers = 1
_progress_path: Path | None = None


def progress(stage: str, **fields):
    record = {"stage": stage, "workers": _workers, **fields}
    if _progress_path:
        temporary = _progress_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        temporary.replace(_progress_path)
    print(json.dumps(record), flush=True)


class BoundedPool:
    """One inline worker: no queued tasks, child heaps or index serialization."""
    def __init__(self, max_workers=None, *args, **kwargs):
        self.initializer = kwargs.pop("initializer", None)
        self.initargs = kwargs.pop("initargs", ())
        if args or kwargs:
            raise ValueError("Unsupported arguments for pinned generator scheduler")

    def __enter__(self):
        if self.initializer:
            self.initializer(*self.initargs)
        from references import build
        build._stats.clear()
        return self

    def __exit__(self, *exception):
        return False

    def map(self, fn, *iterables, timeout=None, chunksize=1, **kwargs):
        if len(iterables) != 1:
            raise ValueError("Pinned generator must use one job iterable")
        tasks = iterables[0]
        total = len(tasks)
        stage = "definitions" if fn.__name__ == "_definitions" else "rewrite"
        progress(stage, completed=0, total=total, percent=0.0)
        started = time.monotonic()
        results = map(fn, tasks)
        for count, result in enumerate(results, 1):
            error = result[-1] if stage == "definitions" else result.error
            if error:
                source = result[0] if stage == "definitions" else result.source
                progress("failed", phase=stage, completed=count, total=total,
                         source=source, error=error)
                raise RuntimeError(f"{source}: {error}")
            if count % 200 == 0 or count == total:
                progress(stage, completed=count, total=total,
                         percent=round(100 * count / total, 2),
                         elapsed_seconds=round(time.monotonic() - started, 1))
            yield result


def stamp(path):
    state = Path(path).stat()
    return [state.st_size, state.st_mtime_ns]


def source_revision(root: Path):
    return subprocess.check_output(
        ["git", "-c", f"safe.directory={root.as_posix()}", "-C", str(root), "rev-parse", "HEAD"],
        text=True).strip()


def verify_pin(root: Path):
    lock = json.loads((Path(__file__).resolve().parents[1] / "native/upstream-lock.json").read_text("utf-8"))
    if source_revision(root) != lock["commit"]:
        raise ValueError("Native upstream is not at the pinned commit")
    if source_revision(root / "external/khdays-decomp") != lock["decomp_commit"]:
        raise ValueError("Decomp is not at the pinned commit")


def verify_failed_files(root, out, error_log):
    from references import build
    from references import preprocess
    tools = build.tools_key()
    build._init(str(out / "references"), tools, "x64")
    if "hostx64" not in build._cl.lower().replace("\\", "/"):
        raise ValueError("The preprocessor must be the HostX64 toolset")
    sources = list(dict.fromkeys(re.findall(r"(?m)^([A-Za-z]:[^\r\n]+\.c): PreprocessError:",
                                           error_log.read_text("utf-8-sig"))))
    if not sources:
        raise ValueError("No failed preprocessing files in the selected log")
    report = []
    for source in sources:
        # Explicitly exercise CL again, even if an older cache entry exists.
        text = preprocess.preprocess(Path(source), build._flags, build._env, build._cl, raw=True)
        result = build._definitions(source)
        if result[-1]:
            raise RuntimeError(f"{source}: {result[-1]}")
        report.append({"source": source, "preprocessed_chars": len(text),
                       "functions": len(result[1]), "objects": len(result[2]), "result": "pass"})
    destination = out / "preflight-low-memory.json"
    destination.write_text(json.dumps({"compiler": build._cl, "workers": 1, "files": report},
                                      indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


def checkpoint_rewrite(original, prepare, root, out, sources, absolute, functions, dispatched):
    caller = inspect.currentframe().f_back.f_back
    locals_ = caller.f_locals
    ds_data = locals_["ds_data"]
    record = {"source": str(root), "prepare_sha256": hashlib.sha256(Path(prepare.__file__).read_bytes()).hexdigest(),
              "sources": sources, "absolute": absolute, "functions": functions, "dispatched": dispatched,
              "source_stamps": {path: stamp(path) for path, _ in sources},
              "ds_symbols": {n: ds_data[n] for n in prepare.PORT_DS_SYMBOLS},
              "tools_key": __import__("references.build", fromlist=["tools_key"]).tools_key()}
    checkpoint = out / "ps5-rewrite-checkpoint.json"
    temporary = checkpoint.with_suffix(".tmp")
    temporary.write_text(json.dumps(record), encoding="utf-8")
    temporary.replace(checkpoint)
    # prepare.main has persisted these texts and does not read them again
    # after rewrite_game. Release them before starting compiler workers.
    released = []
    for name in ("texts", "new_texts", "initialized_texts", "padded_texts", "widened_texts",
                 "narrowed_texts", "unoptimized_texts"):
        value = locals_.get(name)
        if isinstance(value, dict):
            value.clear()
            released.append(name)
    del caller, locals_, record
    gc.collect()
    progress("rewrite checkpoint saved", sources=len(sources), released_text_maps=released)
    return original(out, sources, absolute, functions, dispatched)


def resume_rewrite(prepare, original, root, out, checkpoint):
    from references import build
    record = json.loads(checkpoint.read_text("utf-8"))
    if record["source"] != str(root) or record["tools_key"] != build.tools_key():
        raise ValueError("Rewrite checkpoint belongs to different source/tools")
    if record["prepare_sha256"] != hashlib.sha256(Path(prepare.__file__).read_bytes()).hexdigest():
        raise ValueError("Generator changed after the checkpoint")
    for path, expected in record["source_stamps"].items():
        if stamp(path) != expected:
            raise ValueError(f"Checkpoint input changed: {path}")
    outputs, _ = original(out, record["sources"], record["absolute"],
                          record["functions"], record["dispatched"])
    # This is prepare.main's post-rewrite validation, preserved on resume.
    block = set((out / "references_block_abi.txt").read_text("utf-8").split())
    calls = []
    for directory in ("runtime", "host", "gfx", "sound"):
        for path in (root / "port" / directory).rglob("*.c*"):
            text = prepare.strip_comments(path.read_text("utf-8", errors="replace"))
            calls.extend(f"{path.name}: {n}" for n in block if re.search(rf"\b{re.escape(n)}\s*\(", text))
    if calls:
        raise ValueError("Host calls use the game's block ABI: " + ", ".join(calls))
    symbols = ("/* Generated DS addresses, resumed from the pinned generator checkpoint. */\n"
               "#ifndef KHDAYS_DS_SYMBOLS_H\n#define KHDAYS_DS_SYMBOLS_H\n" +
               "".join(f"#define KHDAYS_DS_{n} 0x{a:08x}u\n" for n, a in record["ds_symbols"].items()) +
               "#endif\n")
    (out / "khdays_ds_symbols.h").write_text(symbols, encoding="utf-8")
    (out / "sources.cmake").write_text(prepare.cmake_list("KHDAYS_REWRITTEN", outputs), encoding="utf-8")


def main():
    global _workers, _progress_path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--jobs", type=int, choices=(1,), default=1)
    parser.add_argument("--preflight-failures", type=Path)
    args = parser.parse_args()
    root, out = args.source.resolve(), args.out.resolve()
    verify_pin(root)
    out.mkdir(parents=True, exist_ok=True)
    temp = out / "temp"
    temp.mkdir(exist_ok=True)
    os.environ["TMP"] = os.environ["TEMP"] = str(temp)
    sys.path.insert(0, str(root / "port/tools"))
    import prepare
    from references import build
    _workers = args.jobs
    _progress_path = out / "generator-state.json"
    if args.preflight_failures:
        verify_failed_files(root, out, args.preflight_failures)
        return 0
    build.ProcessPoolExecutor = BoundedPool
    original = prepare.rewrite_game
    checkpoint = out / "ps5-rewrite-checkpoint.json"
    try:
        if checkpoint.exists():
            progress("resuming reference rewrite from checkpoint")
            resume_rewrite(prepare, original, root, out, checkpoint)
        else:
            progress("preparing source transformations; preserving reference cache")
            prepare.rewrite_game = lambda *values: checkpoint_rewrite(original, prepare, root, *values)
            sys.argv = [str(root / "port/tools/prepare.py"), "--out", str(out)]
            result = prepare.main()
            if result:
                return result
        progress("complete", sources_ready=True)
        return 0
    except BaseException as error:
        progress("failed", error=str(error))
        raise


if __name__ == "__main__":
    raise SystemExit(main())
