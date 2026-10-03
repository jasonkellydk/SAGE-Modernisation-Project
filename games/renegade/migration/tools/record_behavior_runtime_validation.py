"""Record partial saved-startup and input-permission evidence; no mission closure."""
import argparse
import json
from pathlib import Path
import ledger
from record_campaign_validation import digest, passed

RECEIPT="games/renegade/migration/behavior-runtime-validation.json"
TARGETS=";".join((
    "engine/gameplay/common/scripts/components/lua_behavior.cppm",
    "engine/gameplay/common/scripts/resources/behavior_programs.cppm",
    "engine/gameplay/common/scripts/systems/lua_behavior_system.cppm",
    "engine/gameplay/common/scripts/systems/behavior_attachment_system.cppm",
    "engine/gameplay/common/input/components/input_permission.cppm",
    "engine/gameplay/common/input/systems/input_permission_system.cppm",
    "games/renegade/content/campaign/behavior_programs.cppm",
    "games/renegade/content/scripts/MTU_Commando_Startup.lua",
    "games/renegade/content/scripts/MTU_Commando.lua",
    "games/renegade/gameplay/missions/systems/mission_command_system.cppm",
    "games/renegade/gameplay/humans/systems/motion_system.cppm",
    "games/renegade/session/session.cppm",
    "games/renegade/presentation/player/input.cppm",
    "games/renegade/presentation/scene/level_view.cppm",
    "games/renegade/hosts/game/main.cppm"))
TESTS=";".join((
    "engine/gameplay/common/scripts/behavior_events.test",
    "engine/gameplay/common/input/input_permission.test",
    "games/renegade/content/campaign/behavior_programs.test",
    "games/renegade/gameplay/humans/motion.test",
    "games/renegade/presentation/player/input.test",
    "games/renegade/scripting/mission.test",
    "games/renegade/session/scene.test",
    "games/renegade/hosts/game/tutorial_runtime_test.py"))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests",type=Path,required=True)
    parser.add_argument("--runtime",type=Path,required=True)
    parser.add_argument("--build",type=Path,required=True)
    args=parser.parse_args();units,runtime=passed(args.tests),passed(args.runtime)
    required={"engine_gameplay_script_events_tests","engine_gameplay_input_permission_tests","renegade_session_tests",
              "renegade_content_tests","renegade_humans_tests","renegade_player_input_tests","renegade_mission_tests",
              "renegade_level_view_tests","gameplay_architecture","modern_code_policy"}
    if not required<=units or not {"renegade_retail_tutorial_speech.dx11","renegade_retail_tutorial_speech.dx12"}<=runtime:
        raise ValueError("missing attachment, persistent input or running-game evidence")
    base=json.loads((ledger.ROOT/"games/renegade/migration/tutorial-intro-validation.json").read_text())
    if base["result_sha256"]!={str(args.tests):digest(args.tests),str(args.runtime):digest(args.runtime)}:
        raise ValueError("record the fresh tutorial evidence first")
    for name,expected in base["file_sha256"].items():
        if ledger.fingerprint(ledger.ROOT/name)!=expected:raise ValueError("implementation changed since validation")
    evidence=[]
    for backend in ("dx11","dx12"):
        directory=args.build/f"hosts/game/tutorial-speech-{backend}"
        item=json.loads((directory/"receipt.json").read_text())
        if not item.get("saved_player_startup") or not item.get("control_custom_injected") or item.get("control_locked_ticks",0)<12 or item.get("control_restored_displacement_raw",0)<6553:
            raise ValueError("missing actual initialized player observer or locked/restored SDL3 movement")
        if item["executable_sha256"]!=digest(Path(item["command"][0])) or item["log_sha256"]!=digest(directory/"tutorial.log"):
            raise ValueError("stale behavior runtime executable or log")
        evidence.append(item)
    receipt={"revision":ledger.REVISION,"status":"in_progress","claims":[],"tutorial_complete":False,"milestone_2_complete":False,
        "scope":"Saved tutorial startup selects game-local Lua and attaches the commando observer through shared deferred ECS lifecycle. Declared script dependencies prepare off the frame thread, share immutable sources and retain distinct invocation arguments. Source control custom events are directly injected in the live harness to block and restore SDL3 movement through real retail collision; this does not establish lesson-driven control sequencing.",
        "source_references":["Code/Commando/god.cpp:135","Code/Commando/god.cpp:536","Code/Scripts/Mission00.cpp:3345","Code/Scripts/Mission00.cpp:3455"],
        "passed_tests":sorted(units|runtime),"file_sha256":base["file_sha256"],"hash_policy":base["hash_policy"],
        "result_sha256":base["result_sha256"],"runtime_evidence":evidence,
        "limitations":["Partial lesson-driven jump-zone movement/control/facing/camera/dialogue sequencing has separate course evidence. Full camera/action arbitration, player obstacle traversal, complete EVA/combat progression and tutorial completion remain unfinished.",
            "Encyclopedia, weapon selection and Action_Follow_Input arbitration remain pending commands. Commando camera timers and damage branches are unported.",
            "Dynamic attachment Created runs on the next fixed tick after deferred creation. Unsupported attachment programs remain pending; implemented scripts with missing declared dependencies fail preparation.",
            "Checkpoints preserve SoA invocation indices and integer state; tests explicitly reinject the matching immutable program/invocation pool. Complete mission save/load and serialization of that pool are not implemented.",
            "Live checks are Windows DX11/DX12. Production input uses shared SDL3; other platform runtime support is not established."]}
    (ledger.ROOT/RECEIPT).write_text(json.dumps(receipt,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    rows=ledger.load_rows(ledger.MIGRATION)
    identities={"behavior:missions.lua","behavior:campaign.scripts.created_deferred","behavior:campaign.scripts.parameters",
                "script:MTU_Commando","script:MTU_Commando_Startup","callback:MTU_Commando.Created",
                "callback:MTU_Commando.Custom","callback:MTU_Commando_Startup.Created"}
    for row in rows:
        if row["id"] in identities:
            row.update(status="in_progress",target_paths=TARGETS,tests=TESTS,verification=RECEIPT)
            note=" Current increment: saved player startup, shared deferred Lua attachments and persistent input permission; source custom control messages verified with directly injected live events and actual SDL3 movement. Complete callback branches, source lesson sequencing, weapon/action effects and mission saves remain open."
            if note not in row["notes"]:row["notes"]+=note
    ledger.write_rows(rows)
    print("Recorded partial saved-startup/input evidence. Tutorial and milestone 2 remain incomplete.")

if __name__=="__main__":main()
