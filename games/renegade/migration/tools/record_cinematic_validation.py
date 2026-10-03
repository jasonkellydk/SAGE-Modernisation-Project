"""Record narrow opening-film presentation evidence; never close mission or weapon families."""
import argparse
import json
from pathlib import Path
import ledger
from record_campaign_validation import passed, digest

CLAIMS = {
    "campaign.cinematic.opening_visual_tracks": (
        "games/renegade/presentation/scene/cinematic_assets.cppm;games/renegade/presentation/scene/level_view.cppm;games/renegade/gameplay/cinematics/systems/cinematic_system.cppm;engine/gameplay/common/timing/systems/pose_track_system.cppm;games/renegade/content/scripts/MX0_MissionStart_DME.lua",
        "games/renegade/presentation/scene/cinematic_assets.test;games/renegade/session/scene.test;games/renegade/hosts/game/cinematic_runtime_test.py"),
    "campaign.cinematic.equipped_weapon_models": (
        "games/renegade/content/levels/weapon_presentation.cppm;games/renegade/presentation/scene/cinematic_assets.cppm;games/renegade/presentation/scene/level_view.cppm;engine/gameplay/common/appearance/components/attached_model.cppm",
        "games/renegade/presentation/scene/cinematic_assets.test;games/renegade/hosts/game/cinematic_runtime_test.py"),
    "campaign.cinematic.visual_projectiles": (
        "games/renegade/content/scripts/M00_Cinematic_Attack_Command_DLS.lua;games/renegade/content/cinematics/attack.cppm;games/renegade/gameplay/cinematics/systems/cinematic_system.cppm;engine/gameplay/fps/weapons/systems/fire_sequence_system.cppm;engine/gameplay/fps/weapons/systems/projectile_launch_system.cppm;games/renegade/presentation/scene/level_view.cppm",
        "games/renegade/scripting/mission.test;engine/gameplay/fps/weapons/fire_sequence.test;engine/gameplay/fps/weapons/projectile_launch.test;games/renegade/hosts/game/cinematic_runtime_test.py"),
    "campaign.cinematic.projectile_emitters": (
        "engine/assets/adapters/w3d/model/W3DResolvedModel.cppm;engine/assets/model/ModelAsset.cppm;engine/graphics/scene/props/PropAssetPreparation.cppm;engine/graphics/scene/particles/EmitterView.cppm;engine/graphics/scene/particles/EmitterAssetBinding.cppm;games/renegade/presentation/scene/level_view.cppm",
        "engine/assets/adapters/w3d/particles/W3DEmitter.test.cppm;engine/graphics/scene/particles/Emitter.test.cppm;games/renegade/presentation/scene/cinematic_assets.test;games/renegade/hosts/game/cinematic_runtime_test.py"),
}
REQUIRED = {"renegade_level_view_tests", "renegade_session_tests", "renegade_mission_tests",
            "engine_gameplay_fire_sequence_tests", "engine_gameplay_timeline_tests", "engine_gameplay_suspension_tests",
            "generals_assets_w3d_emitter_test", "generals_assets_w3d_rig_test", "generals_assets_w3d_adapter_test",
            "generals_graphics_emitter_test.dx11.hardware", "generals_graphics_emitter_test.dx12.hardware",
            "gameplay_architecture", "modern_code_policy"}
RECEIPT = "games/renegade/migration/cinematic-validation.json"
SPEECH_TARGETS=";".join((
    "engine/assets/model/ModelRig.cppm","engine/assets/model/ModelComposition.cppm",
    "engine/graphics/scene/models/ModelAssetPose.cppm",
    "engine/gameplay/common/scripts/components/timeline_behavior.cppm","engine/gameplay/common/scripts/systems/timeline_behavior_system.cppm",
    "games/renegade/content/scripts/M00_Generic_Conv_DME.lua","games/renegade/content/characters/visemes.cppm",
    "games/renegade/content/presentation/strings.cppm","games/renegade/content/campaign/behavior_programs.cppm",
    "games/renegade/gameplay/missions/components/speech_animation.cppm","games/renegade/gameplay/missions/resources/speech_animations.cppm",
    "games/renegade/gameplay/missions/systems/speech_animation_system.cppm","games/renegade/presentation/scene/speech_assets.cppm",
    "games/renegade/presentation/scene/level_view.cppm","games/renegade/session/session.cppm","games/renegade/hosts/game/main.cppm"))
SPEECH_TESTS=";".join(("games/renegade/session/speech_animation.test","games/renegade/content/cinematics/cinematic.test",
    "games/renegade/content/presentation/strings.test","games/renegade/content/campaign/behavior_programs.test","games/renegade/scripting/mission.test",
    "engine/graphics/scene/models/ModelAssetPose.test.cppm","games/renegade/presentation/scene/cinematic_assets.test","games/renegade/hosts/game/cinematic_runtime_test.py"))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    unit = passed(args.tests)
    runtime = passed(args.runtime)
    if not REQUIRED <= unit or not {"renegade_retail_cinematic.dx11", "renegade_retail_cinematic.dx12"} <= runtime:
        raise ValueError("missing required fresh film, ECS, particle or architecture evidence")
    evidence = []
    for backend in ("dx11", "dx12"):
        directory = args.build / f"hosts/game/cinematic-{backend}"
        item = json.loads((directory / "receipt.json").read_text())
        executable = Path(item["command"][0])
        if item["backend"] != backend or item["returncode"] != 0 or digest(executable) != item["executable_sha256"]:
            raise ValueError("stale film executable/backend evidence")
        if not item.get("projectile_emitter_frames") or not item.get("peak_projectile_particles"):
            raise ValueError("film proof does not distinguish projectile trails from other scene particles")
        if item.get("opening_conversation_attachments")!=26 or item.get("opening_conversation_remarks")!=25 or item.get("missing_source_conversation")!="MX0_GDITROOPER4_HIT6" or item.get("dialogue_only_output") is not True or item.get("speech_output_frames",0)<4800 or item.get("same_tick_speech_pose_pixels",0)<4:
            raise ValueError("missing source conversation, isolated speech or visible facial pose evidence")
        visible = item.get("same_tick_visible_effects", [])
        if len(visible) != 4 or max(sample.get("projectile_mesh_pixels", 0) for sample in visible) < 16 or max(sample.get("projectile_trail_pixels", 0) for sample in visible) < 16:
            raise ValueError("film proof lacks visible projectile meshes/trails in same-tick GPU comparisons")
        if digest(directory / "M13.log") != item["log_sha256"]:
            raise ValueError("stale film log")
        for name, expected in item["captures_sha256"].items():
            if digest(Path(name)) != expected:
                raise ValueError("stale film capture")
        evidence.append(item)
    if len({item["executable_sha256"] for item in evidence}) != 1:
        raise ValueError("backends tested different executables")
    rows = ledger.load_rows(ledger.MIGRATION)
    indexed = {item["id"]: item for item in rows}
    for item in ledger.behavior_items():
        if item["id"] not in indexed:
            indexed[item["id"]] = item
    hashes = {}
    for targets, tests in CLAIMS.values():
        for filename in (targets + ";" + tests).split(";"):
            hashes[filename] = ledger.fingerprint(ledger.ROOT / filename)
    for filename in (SPEECH_TARGETS+";"+SPEECH_TESTS).split(";"):
        hashes[filename]=ledger.fingerprint(ledger.ROOT/filename)
    # Include shared production sources and shaders that determine the frame,
    # so an implementation change invalidates these receipts until rerun.
    base = json.loads((ledger.ROOT / ledger.RECEIPT).read_text())
    for filename, expected in base["file_sha256"].items():
        if ledger.fingerprint(ledger.ROOT / filename) != expected:
            raise ValueError("base validation hashes are stale; record fresh Boost evidence first")
    hashes.update(base["file_sha256"])
    receipt = {"revision": ledger.REVISION, "platform": base["platform"], "tested_at": base["tested_at"],
               "toolchain": base["toolchain"], "hash_policy": base["hash_policy"], "claims": ["behavior:" + key for key in CLAIMS],
               "passed_tests": sorted(unit | runtime), "file_sha256": hashes,
               "milestone_2_complete": False, "runtime_evidence": evidence,
               "scope": "Four narrow opening-film visual slices; full mission, weapon and effects families remain incomplete.",
               "result_sha256": {str(args.tests): digest(args.tests), str(args.runtime): digest(args.runtime)}}
    (ledger.ROOT / RECEIPT).write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    for ident, (targets, tests) in CLAIMS.items():
        item = indexed["behavior:" + ident]
        item.update(status="verified", target_paths=targets, tests=tests, verification=RECEIPT)
    for ident in ("graphics.effects", "graphics.w3d", "weapons.state", "weapons.ballistics"):
        item = indexed.get("behavior:" + ident)
        if item and item["status"] in ("not_started", "in_progress"):
            item.update(status="in_progress", verification=RECEIPT)
    for ident in ("behavior:campaign.cinematic.opening_speech","behavior:campaign.speech.mouth_pose","script:M00_Generic_Conv_DME","callback:M00_Generic_Conv_DME.Created"):
        indexed[ident].update(status="in_progress",target_paths=SPEECH_TARGETS,tests=SPEECH_TESTS,verification=RECEIPT)
    ledger.write_rows(list(indexed.values()))
    print("Recorded four opening-film visual slices; milestone 2 and full combat/effects parity remain open.")

if __name__ == "__main__":
    main()
