"""Record narrow campaign-loading proof; never closes mission or scene families.

Requires freshly built Boost results and the live DX11/DX12 retail preview
harness receipts. Original inventories and historical menu receipts are kept.
"""
import argparse
import hashlib
import json
import xml.etree.ElementTree as ET
from pathlib import Path
import ledger

CLAIMS = {
    "behavior:campaign.load.persist_envelopes": (
        "games/renegade/content/levels/persist_records.cppm;engine/assets/adapters/w3d/chunks/W3DChunkReader.cppm",
        "games/renegade/content/levels/level_scene.test;engine/assets/adapters/w3d/chunks/W3DChunkReader.test.cppm"),
    "behavior:campaign.load.primary_spawner": (
        "games/renegade/content/levels/level_scene.cppm;games/renegade/content/levels/definition_catalog.cppm",
        "games/renegade/content/levels/level_scene.test"),
    "behavior:campaign.scene.affine_soa": (
        "games/renegade/session/session.cppm;engine/gameplay/common/spatial/components/affine_pose.cppm;engine/gameplay/common/spatial/systems/affine_snapshot_system.cppm;engine/core/math/fixed/transform/FixedAffineTransform3.cppm",
        "games/renegade/session/scene.test"),
    "behavior:campaign.player.defense_preset": (
        "games/renegade/content/armor/defense_preset.cppm;games/renegade/content/armor/armor_catalog.cppm",
        "games/renegade/content/armor/defense_preset.test;games/renegade/content/campaign/campaign_content.test"),
    "behavior:campaign.player.difficulty_vitals": (
        "games/renegade/session/session.cppm;games/renegade/content/armor/defense_preset.cppm",
        "games/renegade/session/scene.test;games/renegade/content/armor/defense_preset.test"),
    "behavior:content.camera_profiles": (
        "games/renegade/content/cameras/camera_profiles.cppm;games/renegade/content/campaign/campaign_content.cppm",
        "games/renegade/content/cameras/camera_profiles.test;games/renegade/content/campaign/campaign_content.test"),
}
REQUIRED = {"renegade_levels_tests", "renegade_session_tests", "renegade_content_tests", "renegade_menu_tests",
            "engine_ecs_tests", "engine_gameplay_spatial_tests", "engine_config_tests",
            "generals_assets_w3d_chunks_test", "generals_assets_w3d_mesh_test", "generals_assets_w3d_materials_test",
            "generals_assets_w3d_mesh_data_test", "generals_assets_w3d_model_test"}
RECEIPT = "games/renegade/migration/campaign-loading-validation.json"

def passed(path):
    tree = ET.parse(path); cases = list(tree.getroot().iter("testcase"))
    if not cases or any(case.get("status") != "run" or any(case.find(tag) is not None
            for tag in ("failure", "error", "skipped")) for case in cases):
        raise ValueError(f"requires fully passing, unskipped CTest results: {path}")
    return {case.get("name") for case in cases}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    unit_tests = passed(args.tests); runtime_tests = passed(args.runtime)
    if not REQUIRED <= unit_tests or not {"renegade_retail_level_preview.dx11", "renegade_retail_level_preview.dx12"} <= runtime_tests:
        raise ValueError("missing required campaign or runtime suites")
    executable = args.build / "hosts/game/menu-fix/renegade.exe"
    # This development output override is read from the configured build, so
    # ordinary preset builds can also record without adopting a local folder.
    for line in (args.build / "CMakeCache.txt").read_text().splitlines():
        if line.startswith("RENEGADE_HOST_OUTPUT_DIRECTORY:PATH=") and line.split("=", 1)[1]:
            executable = Path(line.split("=", 1)[1]) / "renegade.exe"
    if not executable.is_file(): executable = args.build / "hosts/game/renegade.exe"
    executable_sha = digest(executable)
    runs = []
    for backend in ("dx11", "dx12"):
        evidence = json.loads((args.build / f"hosts/game/level-preview-{backend}/receipt.json").read_text())
        if evidence["executable_sha256"] != executable_sha or evidence["backend"] != backend:
            raise ValueError("runtime receipt belongs to a different executable or backend; rerun")
        if {run["map"] for run in evidence["runs"]} != {"M00_Tutorial", "M13"}:
            raise ValueError("runtime evidence omits a required retail scene")
        for run in evidence["runs"]:
            if run["returncode"] != 0: raise ValueError("failed runtime process")
            for filename, expected in run["captures_sha256"].items():
                if digest(Path(filename)) != expected: raise ValueError("stale runtime capture")
        runs.append(evidence)
    # Refresh the previously reviewed Boost slices from the same passing run.
    # This recorder cannot manufacture fresh proof for historical menu runs.
    rows = ledger.record(args.tests)
    base = json.loads((ledger.ROOT / ledger.RECEIPT).read_text())
    receipt = {"revision": ledger.REVISION, "platform": base["platform"], "tested_at": base["tested_at"],
        "toolchain": base["toolchain"], "claims": sorted(CLAIMS), "passed_tests": sorted(unit_tests | runtime_tests),
        "file_sha256": base["file_sha256"], "hash_policy": base["hash_policy"],
        "scope": "Narrow fresh-level schema, primary spawn selection, affine SoA snapshots, initial player vitals and typed camera profiles; retail static preview integration",
        "milestone_2_complete": False, "executable_sha256": executable_sha, "runtime_evidence": runs,
        "result_sha256": {str(args.tests): digest(args.tests), str(args.runtime): digest(args.runtime)},
        "limitations": ["No tutorial/campaign completion claim: mission AI, combat/inventory, controllable vehicles/buildings, complete Lua lifecycle, objectives, save/resume and complete HUD remain incomplete. Movement, collision, human animation and EVA pause have separate runtime checks in these previews.",
            "Only Tutorial and M13 have preview runtime captures; counts decode for all thirteen retail mission scenes. Opening cinematics are explicitly bypassed here and have an independent film/shot runtime harness.",
            "Human hierarchy instances, sky/haze and authored ambient sounds are present; complete W3D effects, dazzle, clouds/sun/weather/local/user lighting and SSAO/frame composition remain open.",
            "Authored float matrices/scalars import to Q48.16; integer-only IEEE754 conversion, finite/range diagnostics. Precision differs from original floating simulation.",
            "World checkpoint tests preserve entity columns, not complete mission/scene metadata or script/savegame state.",
            "Windows DX11/DX12 runtime evidence only. Platform/event/config/filesystem code stays shared and portable; non-Windows graphics validation remains open.",
            "Historical menu runtime receipts remain historical after shared source changes; these preview tests verify rendered main-menu recovery, not every old menu flow."]}
    (ledger.ROOT / RECEIPT).write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    indexed = {row["id"]: row for row in rows}
    for row in ledger.behavior_items():
        if row["id"] not in indexed: indexed[row["id"]] = row
    for ident, (targets, tests) in CLAIMS.items():
        row = indexed[ident]; row.update(status="verified", target_paths=targets, tests=tests, verification=RECEIPT)
    for ident in ("behavior:campaign.load.definition_order", "behavior:campaign.load.dynamic_scene", "behavior:campaign.load.static_scene", "behavior:ecs.composition"):
        row = indexed[ident]; row["status"] = "in_progress"; row["verification"] = RECEIPT
    ledger.write_rows(list(indexed.values()))
    print("Recorded six narrow source-backed slices and four live scene processes; milestone 2 remains in progress.")

if __name__ == "__main__": main()
