import tempfile
import unittest
import shutil
import uuid
import json
from unittest.mock import patch
from contextlib import contextmanager
from pathlib import Path
import ledger

@contextmanager
def temporary_directory():
    # Inherit the parent's access rules. Python's Windows mkdtemp uses an
    # owner-only ACL, which prevents child access in a managed build sandbox.
    parent = Path(tempfile.gettempdir()).resolve()
    directory = parent / ("renegade-ledger-" + uuid.uuid4().hex)
    directory.mkdir()
    try:
        yield directory
    finally:
        if directory.resolve().parent != parent:
            raise ValueError("temporary test directory escaped its parent")
        shutil.rmtree(directory)

class LedgerTests(unittest.TestCase):
    def test_comments_and_nested_script_callbacks(self):
        text = '''// DECLARE_SCRIPT(Fake, "") {}
        DECLARE_SCRIPT(Real, "Value:int") { void Created(GameObject *o) { if (true) { auto s = "}"; } }
        void Timer_Expired(GameObject *o, int id) { } };'''
        rows = ledger.script_items("Code/Scripts/example.cpp", text)
        self.assertEqual({r["feature"] for r in rows}, {"Real", "Real.Created", "Real.Timer_Expired"})

    def test_missing_duplicates_and_forged_claims(self):
        rows = ledger.behavior_items()
        inventory = {"items": []}
        self.assertFalse(ledger.validate(rows, inventory, ledger.ROOT))
        self.assertTrue(ledger.validate(rows[:-1], inventory, ledger.ROOT))
        self.assertTrue(ledger.validate(rows + rows[:1], inventory, ledger.ROOT))
        rows[0]["status"] = "verified"
        self.assertTrue(any("required" in error for error in ledger.validate(rows, inventory, ledger.ROOT)))
        rows[0]["target_paths"] = "../../elsewhere.cppm"
        self.assertTrue(any("unsafe" in error for error in ledger.validate(rows, inventory, ledger.ROOT)))

    def test_full_pinned_inventory(self):
        import json
        inventory = json.loads((ledger.MIGRATION / "inventory.json").read_text())
        self.assertEqual(inventory["revision"], ledger.REVISION)
        self.assertGreater(len(inventory["source_sha256"]), 3000)
        self.assertFalse(ledger.validate(ledger.load_rows(ledger.MIGRATION), inventory, ledger.ROOT))

    def test_receipt_rejects_failed_or_skipped_results(self):
        with temporary_directory() as directory:
            path = Path(directory) / "results.xml"
            for body in ('<failure/>', '<skipped/>', '<error/>'):
                path.write_text(f'<testsuite><testcase name="test" status="run">{body}</testcase></testsuite>')
                with self.assertRaises(ValueError): ledger.record(path)

    def test_text_hashes_are_portable_across_checkout_line_endings(self):
        with temporary_directory() as directory:
            path = Path(directory) / "source.cppm"
            path.write_bytes(b"import std;\n")
            expected = ledger.fingerprint(path)
            path.write_bytes(b"import std;\r\n")
            self.assertEqual(expected, ledger.fingerprint(path))

    def test_shader_edits_invalidate_recorded_evidence(self):
        # Exercise the actual recorder and validator in an isolated repository.
        # Shader includes are dependencies even when the claim names a module.
        with temporary_directory() as root:
            migration = root / "games/renegade/migration"
            migration.mkdir(parents=True)
            (root / "engine/cmake/toolchain").mkdir(parents=True)
            (root / "engine/cmake/toolchain/lock.json").write_text('{"versions":{}}')
            shader = root / "engine/calibration.slang"
            shader.write_bytes(b"float calibrate(float x) { return x; }\n")
            include = root / "engine/transfer.slangh"
            include.write_text("// shared transfer definitions\n")
            module = root / "games/renegade/display.cppm"
            module.write_text("export module display;\n")
            (root / "engine/capture.png").write_bytes(b"unrelated artifact")
            result = root / "results.xml"
            result.write_text('<testsuite><testcase name="display" status="run"/></testsuite>')
            rows = ledger.behavior_items()
            claims = {rows[0]["id"]: ("display", "games/renegade/display.cppm", "engine/calibration.slang")}
            ledger_path = migration / "behaviors.csv"
            import csv
            with ledger_path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, ledger.COLUMNS)
                writer.writeheader()
                writer.writerows(rows)
            with patch.object(ledger, "ROOT", root), patch.object(ledger, "MIGRATION", migration), patch.object(ledger, "CLAIMS", claims):
                recorded = ledger.record(result)
                receipt = json.loads((root / ledger.RECEIPT).read_text())
                self.assertIn("engine/calibration.slang", receipt["file_sha256"])
                self.assertIn("engine/transfer.slangh", receipt["file_sha256"])
                self.assertNotIn("engine/capture.png", receipt["file_sha256"])
                self.assertFalse(ledger.validate(recorded, {"items": []}, root))
                shader.write_bytes(shader.read_bytes().replace(b"\n", b"\r\n"))
                self.assertFalse(ledger.validate(recorded, {"items": []}, root))
                include.write_text("// changed shared transfer definitions\n")
                self.assertTrue(any("stale validation receipt: engine/transfer.slangh" in error
                                    for error in ledger.validate(recorded, {"items": []}, root)))

if __name__ == "__main__": unittest.main()
