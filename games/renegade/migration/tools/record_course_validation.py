"""Record partial retail jump-sequence evidence without claiming tutorial completion."""
import argparse
import json
from pathlib import Path
import ledger
from record_campaign_validation import digest, passed

RECEIPT = "games/renegade/migration/tutorial-course-validation.json"
TARGETS = ";".join((
    "engine/camera/motion/anchor_transition.cppm",
    "engine/gameplay/common/appearance/components/clip_playback.cppm",
    "games/renegade/content/cameras/camera_profiles.cppm",
    "engine/level/model/level.cppm",
    "games/renegade/content/levels/transition_definitions.cppm",
    "games/renegade/content/levels/traversal_portals.cppm",
    "games/renegade/gameplay/humans/resources/ladders.cppm",
    "games/renegade/gameplay/humans/systems/ladder_transition_system.cppm",
    "games/renegade/gameplay/humans/systems/animation_system.cppm",
    "engine/core/math/fixed/transform/FixedLookAngles.cppm",
    "engine/gameplay/common/spatial/resources/heading_requests.cppm",
    "engine/gameplay/common/spatial/systems/heading_target_system.cppm",
    "engine/gameplay/common/spatial/systems/rigid_heading_system.cppm",
    "engine/gameplay/common/areas/resources/trigger_volumes.cppm",
    "engine/gameplay/common/areas/systems/volume_presence_system.cppm",
    "engine/gameplay/common/spatial/resources/position_requests.cppm",
    "engine/gameplay/common/spatial/systems/position_request_system.cppm",
    "games/renegade/content/scripts/MTU_Trigger_Zone.lua",
    "games/renegade/content/scripts/MTU_Tutorial_Instructor.lua",
    "games/renegade/gameplay/missions/systems/mission_command_system.cppm",
    "games/renegade/gameplay/missions/resources/camera_requests.cppm",
    "games/renegade/gameplay/humans/systems/motion_system.cppm",
    "games/renegade/presentation/player/input.cppm",
    "games/renegade/session/session.cppm",
    "games/renegade/presentation/scene/level_view.cppm",
    "games/renegade/hosts/game/main.cppm"))
TARGETS+=";"+";".join(("engine/gameplay/common/timing/components/simulation_age.cppm","engine/gameplay/common/timing/systems/simulation_age_system.cppm",
    "engine/gameplay/common/spatial/components/tracked_position.cppm","engine/gameplay/common/spatial/systems/tracked_position_system.cppm",
    "games/renegade/gameplay/missions/components/objective.cppm","games/renegade/gameplay/missions/systems/objective_system.cppm","games/renegade/hud/objective_view_model.cppm"))
TESTS = ";".join((
    "engine/camera/camera.test",
    "engine/gameplay/common/appearance/clip_playback.test",
    "games/renegade/content/cameras/camera_profiles.test",
    "engine/level/model/level.test",
    "games/renegade/content/levels/level_scene.test",
    "games/renegade/gameplay/humans/ladder.test",
    "games/renegade/gameplay/humans/motion.test",
    "games/renegade/gameplay/humans/animation.test",
    "engine/core/math/fixed/tests/FixedLookAngles.test",
    "engine/gameplay/common/spatial/heading_requests.test",
    "engine/gameplay/common/areas/volume_presence.test",
    "engine/gameplay/common/spatial/position_requests.test",
    "games/renegade/scripting/mission.test",
    "games/renegade/session/scene.test",
    "games/renegade/presentation/player/input.test",
    "games/renegade/hosts/game/tutorial_course_runtime_test.py"))
TESTS+=";"+";".join(("engine/gameplay/common/timing/simulation_age.test","engine/gameplay/common/spatial/tracked_position.test",
    "games/renegade/session/objective.test","games/renegade/hud/objective_view_model.test"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    units, runtime = passed(args.tests), passed(args.runtime)
    required = {"engine_camera_tests", "engine_gameplay_appearance_tests", "engine_level_tests", "renegade_levels_tests", "engine_core_math_fixed_tests", "engine_gameplay_areas_tests", "engine_gameplay_spatial_tests", "engine_gameplay_input_permission_tests",
                "engine_gameplay_route_traversal_tests", "renegade_session_tests", "renegade_mission_tests",
                "renegade_humans_tests", "renegade_level_view_tests", "gameplay_architecture", "modern_code_policy"}
    if not required <= units or not {"renegade_retail_tutorial_course.dx11", "renegade_retail_tutorial_course.dx12"} <= runtime:
        raise ValueError("missing fresh shared trigger, relocation, source Lua or live course evidence")
    base = json.loads((ledger.ROOT / "games/renegade/migration/tutorial-intro-validation.json").read_text())
    reports = {str(args.tests): digest(args.tests), str(args.runtime): digest(args.runtime)}
    if base["result_sha256"] != reports:
        raise ValueError("record fresh campaign and tutorial evidence first")
    for name, expected in base["file_sha256"].items():
        if ledger.fingerprint(ledger.ROOT / name) != expected:
            raise ValueError("implementation changed since validation")
    evidence = []
    for backend in ("dx11", "dx12"):
        directory = args.build / f"hosts/game/tutorial-course-{backend}"
        item = json.loads((directory / "receipt.json").read_text())
        if item["backend"] != backend or item["returncode"] or digest(Path(item["command"][0])) != item["executable_sha256"]:
            raise ValueError("stale course executable or backend evidence")
        if not item.get("player_placement_fixture") or item.get("lesson_callbacks_injected") is not False or item.get("NPC_completions_injected") is not False:
            raise ValueError("course fixture must distinguish placement from natural script/action progression")
        if item.get("tutorial_complete") or item.get("milestone_2_complete") or item.get("trigger_id") != 400015:
            raise ValueError("course evidence overclaims completion or uses a different source zone")
        if item.get("natural_route_displacement_raw", 0) < 65536 or item.get("remarks") != 5 or item.get("voiced_remarks") != 5 or item.get("non_silent_frames", 0) < 4800:
            raise ValueError("missing actual route movement or original voiced jump remarks")
        if not item.get("source_camera_facing") or not item.get("camera_body_heading_held") or item.get("camera_alignment",0)<0.999:
            raise ValueError("missing actual source actor/camera alignment with locked player body")
        if any(abs(raw/65536-expected)>2/65536 for raw,expected in zip(item.get("camera_target_raw",[]),(-53.697,-5.334,3.5))) or len(item.get("camera_target_raw",[]))!=3:
            raise ValueError("camera target does not match original relocation and actor height offset")
        if not item.get("ladder_placement_fixture") or item.get("ladder_transition_callbacks_injected") is not False or not item.get("ladder_entry_ascent_exit") or item.get("ladder_climbing_ticks",0)<1 or item.get("ladder_exit_distance_raw",999999)>13108:
            raise ValueError("missing original ladder SDL3/GPU/movement/automatic exit proof")
        if item.get("second_ladder_placement") is not True or not item.get("second_ladder_entry_descent_exit") or item.get("second_ladder_climbing_ticks",0)<1 or item.get("second_ladder_exit_distance_raw",999999)>13108:
            raise ValueError("missing declared second placement and original ladder descent/exit")
        if not item.get("animated_transition_path") or min(item.get("animated_exit_frames",0),item.get("animated_descent_frames",0))<10 or min(item.get("animated_ascent_metres",0),item.get("animated_descent_metres",0))<1.2 or item.get("camera_transition_previous_weight",0)<.5 or not item.get("ladder_view_restores_first_person"):
            raise ValueError("missing sustained original transition poses, smoothing or ladder view restoration")
        if len(item.get("transition_body_visible_pixels",[]))!=2 or min(item["transition_body_visible_pixels"])<64:
            raise ValueError("missing actual same-state GPU transition body visibility comparison")
        if item.get("EVA_zone_placement_fixture") is not True or item.get("EVA_zone")!=400016 or item.get("EVA_remarks")!=5 or item.get("poke_remarks")!=10 or item.get("objective_id")!=1 or item.get("objective_priority")!=99 or item.get("objective_hud_pixels",0)<64:
            raise ValueError("missing actual original EVA/poke completion and localized objective HUD")
        if digest(directory / "course.log") != item["log_sha256"] or len(item["captures_sha256"]) < 15:
            raise ValueError("stale course log or missing GPU captures")
        for name, expected in item["captures_sha256"].items():
            if digest(Path(name)) != expected:
                raise ValueError("stale course GPU capture")
        evidence.append(item)
    if len({item["executable_sha256"] for item in evidence + base["runtime_evidence"]}) != 1:
        raise ValueError("course and intro evidence tested different executables")
    receipt = {"revision": ledger.REVISION, "status": "in_progress", "claims": [],
        "tutorial_complete": False, "milestone_2_complete": False,
        "scope": "Original obstacle-course Lua traces, shared trigger retirement, ordered relocation/rigid heading and typed script camera commands. Retail placement fixtures activate five source zones. The live jump fixture naturally completes Logan's jump/EVA routes, faces him toward the player, applies the source camera target, plays five remarks and renders the instruction. Shared level portals preserve every retail ladder region/destination. Separate upright placements precede SDL3 E/W/S, shared once transition clips and natural exits. Intermediate authored ascent/descent poses are GPU-captured, with sustained 1.2m+ body motion and original camera-mode restoration. This is not a player tutorial playthrough; inter-ladder course movement remains unfinished.",
        "source_references": ["Code/Combat/humanstate.cpp:519", "Code/Combat/combat.cpp:1156", "Code/Combat/ccamera.cpp:738", "Code/Combat/transition.cpp:273", "Code/Combat/transition.cpp:513", "Code/Combat/transitiongameobj.cpp:87", "Code/Combat/soldier.cpp:1699", "Code/Combat/humanstate.cpp:869", "Code/wwphys/phys3.cpp:1357", "Code/Scripts/Mission00.cpp:1308", "Code/Scripts/Mission00.cpp:2360",
            "Code/Scripts/Mission00.cpp:2687", "Code/Scripts/Mission00.cpp:2757", "Code/Combat/scriptcommands.cpp:290",
            "Code/Combat/scriptcommands.cpp:341", "Code/Combat/ccamera.cpp:1545"],
        "passed_tests": sorted(units | runtime), "file_sha256": base["file_sha256"], "hash_policy": base["hash_policy"],
        "result_sha256": reports, "runtime_evidence": evidence,
        "limitations": ["Speaking animation, full camera/action arbitration, complete objective/equipment state and player obstacle-course traversal remain unfinished. Literal camera points have source equation/typed-dispatch fixtures; live literal-point use remains unverified.",
            "Separate ladder placements and upright states are setup fixtures; full crate-course traversal remains open. Published transition.cpp disables Start_Transition_Animation; the port deliberately restores that implemented path using original retail clips whose body channels cover 1.606m vertically. Authoritative destinations apply at transition start; exclusive playheads and camera interpolation supply visible travel. Ladder footsteps, equipped-weapon hiding, complete soldier-state arbitration, AI path actions and indexed occupancy remain open; all retail placements save index -1.",
            "Six hidden source objectives now use serializable SoA state, integer-tick ages and shared actor/point tracking. The EVA placement fixture naturally completes five EVA and ten poke remarks and displays objective 1's retail icon, localized label, arrow and range with a same-state GPU comparison. Weapon selection, radar/notification windows, keycard spawning and direct-destination effects remain pending. Later trigger-zone branches remain unported.",
            "Shared relocation sets position only and preserves velocity, facing, scale and rotation. Full teleport collision/arbitration and static-object collision removal remain open.",
            "Facing resolves after completed position requests. Arbitrary interleavings of multiple relocations and cross-entity facing within one tick need additional source transaction fixtures.",
            "Dead trigger contacts retire on the next completed ECS barrier without exit callbacks. Other shared callers retain their existing opaque-owner/removal policy by default.",
            "Camera look rejects a zero-distance target instead of retaining the legacy asin(0/0) NaN. Ordinary mouse tilt now uses the source +/-80 degree limit; script look is independent of input permission.",
            "Only Windows DX11/DX12 live coverage; other platforms remain unvalidated."]}
    (ledger.ROOT / RECEIPT).write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    rows = ledger.load_rows(ledger.MIGRATION)
    indexed = {row["id"]: row for row in rows}
    for row in ledger.behavior_items():
        indexed.setdefault(row["id"], row)
    identities = {"behavior:campaign.tutorial.jump_sequence", "behavior:campaign.tutorial.camera_facing", "behavior:campaign.tutorial.ladders", "script:MTU_Trigger_Zone",
        "callback:MTU_Trigger_Zone.Created", "callback:MTU_Trigger_Zone.Entered",
        "script:MTU_Tutorial_Instructor", "callback:MTU_Tutorial_Instructor.Custom",
        "callback:MTU_Tutorial_Instructor.Action_Complete"}
    for identity in identities:
        indexed[identity].update(status="in_progress", target_paths=TARGETS, tests=TESTS, verification=RECEIPT)
    ledger.write_rows(indexed.values())
    print("Recorded partial jump-course evidence; tutorial and milestone 2 remain incomplete.")


if __name__ == "__main__":
    main()
