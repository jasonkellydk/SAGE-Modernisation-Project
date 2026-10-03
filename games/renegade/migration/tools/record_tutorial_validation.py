"""Record four narrow tutorial intro slices; never close a mission or script family."""
import argparse
import json
from pathlib import Path
import ledger
from record_campaign_validation import passed, digest

CLAIMS = {
    "campaign.tutorial.entrance_gates": (
        "engine/level/model/collision_mesh.cppm;engine/level/collision/collision_scene.cppm;engine/level/presentation/model_collision.cppm;engine/gameplay/common/appearance/resources/clip_requests.cppm;engine/gameplay/common/appearance/systems/clip_request_system.cppm;engine/gameplay/common/physics/components/kinematic_collider.cppm;engine/gameplay/common/physics/resources/kinematic_collision.cppm;engine/gameplay/common/physics/systems/kinematic_collision_system.cppm;games/renegade/content/campaign/behavior_programs.cppm;games/renegade/content/scripts/MTU_Tutorial_Controller.lua;games/renegade/content/scripts/MTU_Tutorial_Instructor.lua;games/renegade/gameplay/missions/resources/animation_library.cppm;games/renegade/gameplay/missions/systems/mission_command_system.cppm;games/renegade/session/session.cppm;games/renegade/presentation/scene/level_view.cppm",
        "engine/level/collision/collision_scene.test;engine/level/presentation/model_collision.test;engine/gameplay/common/physics/kinematic_collision.test;games/renegade/presentation/scene/cinematic_assets.test;games/renegade/scripting/mission.test;games/renegade/session/scene.test;games/renegade/hosts/game/tutorial_runtime_test.py"),
    "campaign.tutorial.intro_dispatch": (
        "engine/scripting/lua/lua_program.cppm;engine/gameplay/common/scripts/systems/lua_behavior_system.cppm;engine/gameplay/common/scripts/systems/message_timer_system.cppm;games/renegade/content/campaign/behavior_programs.cppm;games/renegade/gameplay/missions/systems/mission_command_system.cppm;games/renegade/content/scripts/MTU_Tutorial_Controller.lua;games/renegade/content/scripts/MTU_Tutorial_Instructor.lua",
        "engine/gameplay/common/scripts/behavior_events.test;games/renegade/scripting/mission.test;games/renegade/session/scene.test;games/renegade/hosts/game/tutorial_runtime_test.py"),
    "campaign.tutorial.intro_speech": (
        "games/renegade/content/campaign/conversations.cppm;games/renegade/content/audio/sound_preset.cppm;games/renegade/gameplay/missions/components/conversation_state.cppm;games/renegade/gameplay/missions/systems/conversation_system.cppm;games/renegade/presentation/scene/level_view.cppm",
        "games/renegade/content/campaign/conversations.test;games/renegade/session/conversation.test;games/renegade/hosts/game/tutorial_runtime_test.py"),
    "campaign.tutorial.intro_help_text": (
        "games/renegade/content/scripts/MTU_Tutorial_Instructor.lua;games/renegade/hud/help_text_view_model.cppm;games/renegade/presentation/scene/level_view.cppm;games/renegade/hosts/game/main.cppm;engine/gui/w3d/rendering/text_view.cppm;engine/time/value_transition.cppm",
        "games/renegade/scripting/mission.test;games/renegade/hud/help_text_view_model.test;games/renegade/hosts/game/tutorial_runtime_test.py"),
}
REQUIRED = {"renegade_levels_tests", "renegade_session_tests", "renegade_mission_tests", "renegade_hud_tests",
            "engine_level_tests", "engine_level_assets_tests", "engine_gameplay_appearance_tests", "engine_gameplay_kinematic_collision_tests", "renegade_level_view_tests",
            "engine_scripting_lua_tests", "engine_gameplay_script_events_tests", "engine_gameplay_timeline_tests",
            "engine_gui_w3d_tests", "gameplay_architecture", "modern_code_policy"}
RECEIPT = "games/renegade/migration/tutorial-intro-validation.json"

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests",type=Path,required=True)
    parser.add_argument("--runtime",type=Path,required=True)
    parser.add_argument("--build",type=Path,required=True)
    args=parser.parse_args();unit=passed(args.tests);runtime=passed(args.runtime)
    if not REQUIRED<=unit or not {"renegade_retail_tutorial_speech.dx11","renegade_retail_tutorial_speech.dx12"}<=runtime:
        raise ValueError("missing required tutorial, ECS, Lua, HUD or architecture evidence")
    evidence=[]
    for backend in ("dx11","dx12"):
        directory=args.build/f"hosts/game/tutorial-speech-{backend}"
        item=json.loads((directory/"receipt.json").read_text())
        if item["backend"]!=backend or item["returncode"] or digest(Path(item["command"][0]))!=item["executable_sha256"]:
            raise ValueError("stale tutorial executable/backend evidence")
        if item.get("tutorial_complete") or item.get("milestone_2_complete") or item.get("remarks",0)<1 or item.get("voiced_remarks",0)<1 or item.get("non_silent_frames",0)<4800:
            raise ValueError("tutorial evidence overclaims completion or lacks original speech")
        if item.get("gate_visible_pixels",0)<16 or item.get("gate_capture_tick",0)<1 or len(item["captures_sha256"])<5:
            raise ValueError("tutorial evidence lacks visible gates or closed/open collision validation")
        if not item.get("source_camera_facing") or item.get("camera_alignment",0)<0.999:
            raise ValueError("tutorial evidence lacks original actor-facing and camera look alignment")
        if digest(directory/"tutorial.log")!=item["log_sha256"]:raise ValueError("stale tutorial log")
        for name,expected in item["captures_sha256"].items():
            if digest(Path(name))!=expected:raise ValueError("stale tutorial capture")
        evidence.append(item)
    if len({item["executable_sha256"] for item in evidence})!=1:raise ValueError("tutorial backends tested different executables")
    base=json.loads((ledger.ROOT/ledger.RECEIPT).read_text())
    for name,expected in base["file_sha256"].items():
        if ledger.fingerprint(ledger.ROOT/name)!=expected:raise ValueError("record fresh Boost evidence before tutorial evidence")
    rows=ledger.load_rows(ledger.MIGRATION);indexed={item["id"]:item for item in rows}
    for item in ledger.behavior_items():indexed.setdefault(item["id"],item)
    receipt={"revision":ledger.REVISION,"platform":base["platform"],"tested_at":base["tested_at"],
        "toolchain":base["toolchain"],"hash_policy":base["hash_policy"],"file_sha256":base["file_sha256"],
        "claims":["behavior:"+name for name in CLAIMS],"passed_tests":sorted(unit|runtime),
        "runtime_evidence":evidence,"milestone_2_complete":False,"tutorial_complete":False,
        "scope":"Four narrow original tutorial intro slices, including translated entrance gate rendering/collision; all later tutorial stages, obstruction push/crush, rotating collision and broad script/HUD families remain incomplete.",
        "result_sha256":{str(args.tests):digest(args.tests),str(args.runtime):digest(args.runtime)}}
    (ledger.ROOT/RECEIPT).write_text(json.dumps(receipt,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    for name,(targets,tests) in CLAIMS.items():
        indexed["behavior:"+name].update(status="verified",target_paths=targets,tests=tests,verification=RECEIPT)
    # Ported startup branches are not evidence that an entire legacy callback
    # or script is complete. Preserve the inventory and keep these families open.
    for name in ("MTU_Tutorial_Controller","MTU_Tutorial_Instructor"):
        for item in indexed.values():
            if item["kind"] in {"script","callback"} and (item["feature"]==name or item["feature"].startswith(name+".")) and item["status"]=="not_started":
                item.update(status="in_progress",verification=RECEIPT)
    ledger.write_rows(list(indexed.values()))
    print("Recorded four tutorial intro slices. Tutorial and milestone 2 remain incomplete.")

if __name__=="__main__":main()
