"""Compare the bounded scheduler with the real upstream reference rewrite."""
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from types import SimpleNamespace

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT / ".cache/khdays-native-v0.1.1"
sys.path.insert(0, str(PROJECT / "scripts"))
sys.path.insert(0, str(ROOT / "port/tools"))
import prepare_native_bounded as bounded
from references import build


class BoundedGeneratorTest(unittest.TestCase):
    def test_checkpoint_releases_persisted_text_but_preserves_symbols(self):
        work = PROJECT / "build-native-generator-check/checkpoint"
        work.mkdir(parents=True, exist_ok=True)
        source = work / "source.c"
        source.write_text("int value;\n", encoding="utf-8")
        module = SimpleNamespace(__file__=str(ROOT / "port/tools/prepare.py"), PORT_DS_SYMBOLS=["example"])
        bounded._progress_path = work / "generator-state.json"

        def simulated_prepare_main():
            ds_data = {"example": 0x02000000}
            texts = {"source": "already written"}

            def original(*args):
                self.assertEqual(texts, {})
                self.assertEqual(ds_data, {"example": 0x02000000})
                return [], 0

            callback = lambda *args: bounded.checkpoint_rewrite(original, module, ROOT.resolve(), *args)
            return callback(work, [(str(source), "source.c")], {}, {}, {})

        self.assertEqual(simulated_prepare_main(), ([], 0))
        record = json.loads((work / "ps5-rewrite-checkpoint.json").read_text("utf-8"))
        self.assertEqual(record["ds_symbols"], {"example": 0x02000000})
        self.assertEqual(record["source_stamps"][str(source)], bounded.stamp(source))

    def test_real_rewrite_then_reuse_without_preprocessing(self):
        work = PROJECT / "build-native-generator-check"
        inputs = work / "inputs"
        inputs.mkdir(parents=True, exist_ok=True)
        first = inputs / "first.c"
        second = inputs / "second.c"
        for path, text in ((first, "int object = 7; int *reference(void) { return &object; }\n"),
                           (second, "extern int object; int read_object(void) { return object; }\n")):
            if not path.exists() or path.read_text("utf-8") != text:
                path.write_text(text, encoding="utf-8")
        original_pool = build.ProcessPoolExecutor
        output = work / "bounded"
        output.mkdir(exist_ok=True)
        bounded._progress_path = output / "generator-state.json"
        build.ProcessPoolExecutor = bounded.BoundedPool
        jobs = [(str(source), str(output / source.name)) for source in (first, second)]
        try:
            paths, size = build.rewrite_all(jobs, build.Program(absolute={}, functions={}), output, processes=1)
            self.assertGreater(size, 0)
            texts = [Path(p).read_text("utf-8") for p in paths]
            header = (output / "khdays_data_area.h").read_text("utf-8")
            self.assertIn('#include "khdays_references.h"', texts[0])
            self.assertIn(".khdi$m", texts[0])
            self.assertIn("KHDAYS_DATA_AREA_END", header)
            with patch("references.preprocess.preprocess", side_effect=AssertionError("Cache was not reused")):
                second_paths, second_size = build.rewrite_all(
                    jobs, build.Program(absolute={}, functions={}), output, processes=1)
            self.assertEqual(size, second_size)
            self.assertEqual(texts, [Path(p).read_text("utf-8") for p in second_paths])
            self.assertEqual(header, (output / "khdays_data_area.h").read_text("utf-8"))
            (work / "validation.json").write_text(json.dumps({
                "result": "pass", "translation_units": 2,
                "cached_output_identical": True, "workers": 1,
                "pending_task_buffer": 0}, indent=2) + "\n", encoding="utf-8")
        finally:
            build.ProcessPoolExecutor = original_pool


if __name__ == "__main__":
    unittest.main()
