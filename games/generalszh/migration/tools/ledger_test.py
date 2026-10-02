#!/usr/bin/env python3
"""Self-test for ledger.py against a miniature fake legacy tree."""

import tempfile
import unittest
from pathlib import Path

import ledger

FILES = {
    ledger.MODULE_FACTORIES[0]: "void f() {\n addModule( AutoHealBehavior );\n // addModule( Commented );\n}\n",
    ledger.MODULE_FACTORIES[1]: "void g() { addModule( W3DModelDraw ); }\n",
    ledger.SCRIPTS_HEADER: (
        "class ScriptAction {\n enum ScriptActionType\n {\n  DEBUG_MESSAGE_BOX, ///< a\n"
        "  SET_FLAG,\n  NUM_ITEMS\n };\n};\n"
        "class Condition {\n enum ConditionType\n {\n  CONDITION_FALSE,\n  COUNTER, // x\n  NUM_ITEMS\n };\n};\n"),
    ledger.INI_TYPE_TABLE: 'static const BlockParse theTypeTable[] =\n{\n\t{ "Armor", INI::parseArmor },\n\t{ nullptr, nullptr },\n};\n',
    ledger.GAME_ENGINE: 'void init() {\n initSubsystem(TheLocalFileSystem, "TheLocalFileSystem", create(), nullptr);\n}\n',
    "GeneralsMD/Code/GameEngine/Include/GameLogic/Module/AutoHealBehavior.h": (
        "class AutoHealBehaviorModuleData : public UpdateModuleData\n{\n"
        " static void buildFieldParse(MultiIniFieldParse& p)\n {\n"
        "  static const FieldParse dataFieldParse[] =\n  {\n"
        '   { "Radius", INI::parseReal, nullptr, 0 },\n'
        '   // { "Disabled", INI::parseReal, nullptr, 0 },\n'
        '   { "Odd{Name}", INI::parseReal, nullptr, 0 },\n'
        "   { nullptr, nullptr, nullptr, 0 }\n  };\n }\n};\n"),
    "Core/GameEngine/Source/Common/Weapon.cpp": (
        'const FieldParse WeaponTemplate::TheWeaponTemplateFieldParseTable[] =\n{\n'
        '\t{ "PrimaryDamage", nullptr, nullptr, 0 },\n\t{ nullptr, nullptr, nullptr, 0 }\n};\n'),
}


class LedgerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for rel, text in FILES.items():
            path = self.root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        self.ledger = self.root / "ledger"

    def tearDown(self):
        self.temp.cleanup()

    def run_tool(self, command):
        return ledger.main([command, "--root", str(self.root), "--ledger", str(self.ledger)])

    def test_scan_finds_every_registry_and_ignores_comments(self):
        ids = set(ledger.scan_legacy(self.root))
        self.assertEqual(ids, {
            "module:AutoHealBehavior", "module:W3DModelDraw",
            "script_action:DEBUG_MESSAGE_BOX", "script_action:SET_FLAG",
            "script_condition:CONDITION_FALSE", "script_condition:COUNTER",
            "ini_block:Armor", "subsystem:TheLocalFileSystem",
            "ini_field:AutoHealBehaviorModuleData.Radius",
            "ini_field:AutoHealBehaviorModuleData.Odd{Name}",
            "ini_field:WeaponTemplate.PrimaryDamage",
        })

    def test_check_fails_until_synced(self):
        self.assertEqual(self.run_tool("check"), 1)
        self.assertEqual(self.run_tool("sync"), 0)
        self.assertEqual(self.run_tool("check"), 0)

    def test_new_legacy_item_breaks_check(self):
        self.run_tool("sync")
        factory = self.root / ledger.MODULE_FACTORIES[0]
        factory.write_text(factory.read_text() + "void h() { addModule( NewBehavior ); }\n")
        self.assertEqual(self.run_tool("check"), 1)

    def test_sync_preserves_edits(self):
        self.run_tool("sync")
        rows = ledger.load(self.ledger)
        rows["module:AutoHealBehavior"]["notes"] = "kept"
        (self.ledger / "modules.csv").write_text(
            ledger.render([r for r in rows.values() if r["kind"] == "module"]))
        self.run_tool("sync")
        self.assertEqual(ledger.load(self.ledger)["module:AutoHealBehavior"]["notes"], "kept")

    def row(self, **values):
        base = {column: "" for column in ledger.COLUMNS}
        base.update({"id": "module:X", "kind": "module", "status": "not_started"})
        base.update(values)
        return {"module:X": base}

    def test_ported_requires_existing_modern_files_inside_placement(self):
        (self.root / "engine/gameplay/common/healing/systems").mkdir(parents=True)
        (self.root / "engine/gameplay/common/healing/systems/heal.cppm").write_text("")
        (self.root / "engine/gameplay/common/healing/systems/heal.test").write_text("")
        good = self.row(status="ported", placement="engine/gameplay/common",
                        target_paths="engine/gameplay/common/healing/systems/heal.cppm",
                        tests="engine/gameplay/common/healing/systems/heal.test")
        self.assertEqual(ledger.validate(self.root, good), [])
        self.assertTrue(ledger.validate(self.root, self.row(status="ported")))
        missing = self.row(status="ported", placement="engine/gameplay/common",
                           target_paths="engine/gameplay/common/missing.cppm", tests="x.test")
        self.assertEqual(len(ledger.validate(self.root, missing)), 2)
        outside = dict(good["module:X"], placement="games/generalszh")
        self.assertTrue(ledger.validate(self.root, {"module:X": outside}))

    def test_rules_for_other_statuses(self):
        self.assertTrue(ledger.validate(self.root, self.row(status="not_applicable")))
        self.assertEqual(ledger.validate(self.root, self.row(status="not_applicable", notes="editor only")), [])
        self.assertTrue(ledger.validate(self.root, self.row(status="done")))
        self.assertTrue(ledger.validate(self.root, self.row(target_paths="GeneralsMD/Code/x.cpp")))
        self.assertTrue(ledger.validate(self.root, self.row(placement="src/whatever")))


if __name__ == "__main__":
    unittest.main()
