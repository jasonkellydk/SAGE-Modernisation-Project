"""Record partial navigation evidence without closing any AI or mission family."""
import argparse
import json
from pathlib import Path
import ledger
from record_campaign_validation import digest, passed

RECEIPT = "games/renegade/migration/navigation-validation.json"
TARGETS = ";".join((
    "engine/level/model/level.cppm", "engine/level/model/path_curve.cppm",
    "engine/gameplay/common/movement/components/route_traversal.cppm",
    "engine/gameplay/common/movement/resources/route_curves.cppm",
    "engine/gameplay/common/movement/systems/route_traversal_system.cppm",
    "games/renegade/content/levels/navigation_paths.cppm",
    "games/renegade/gameplay/humans/components/goto_action.cppm",
    "games/renegade/gameplay/humans/systems/goto_system.cppm",
    "games/renegade/gameplay/missions/resources/routes.cppm",
    "games/renegade/gameplay/missions/systems/mission_command_system.cppm",
    "games/renegade/content/characters/movement_definition.cppm",
    "games/renegade/session/session.cppm", "games/renegade/presentation/scene/level_view.cppm"))
TESTS = ";".join((
    "engine/level/model/level.test", "engine/level/model/path_curve.test",
    "engine/gameplay/common/movement/route_traversal.test",
    "games/renegade/content/levels/level_scene.test",
    "games/renegade/content/characters/movement_definition.test",
    "games/renegade/gameplay/humans/goto.test",
    "games/renegade/hosts/game/tutorial_runtime_test.py", "games/renegade/hosts/game/tutorial_course_runtime_test.py"))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    units, runtime = passed(args.tests), passed(args.runtime)
    required = {"engine_level_tests", "engine_gameplay_route_traversal_tests", "renegade_levels_tests",
                "renegade_session_tests", "renegade_humans_tests", "renegade_content_tests", "renegade_level_view_tests"}
    if not required <= units or not {"renegade_retail_tutorial_speech.dx11", "renegade_retail_tutorial_speech.dx12", "renegade_retail_tutorial_course.dx11", "renegade_retail_tutorial_course.dx12"} <= runtime:
        raise ValueError("missing navigation, soldier or live tutorial regression evidence")
    base = json.loads((ledger.ROOT / "games/renegade/migration/tutorial-intro-validation.json").read_text())
    if base["result_sha256"] != {str(args.tests): digest(args.tests), str(args.runtime): digest(args.runtime)}:
        raise ValueError("record the fresh campaign/cinematic/tutorial evidence first")
    executable = args.build / "hosts/game/menu-fix/renegade.exe"
    for line in (args.build / "CMakeCache.txt").read_text().splitlines():
        if line.startswith("RENEGADE_HOST_OUTPUT_DIRECTORY:PATH=") and line.split("=",1)[1]:
            executable = Path(line.split("=",1)[1]) / "renegade.exe"
    if not executable.is_file(): executable = args.build / "hosts/game/renegade.exe"
    executable_sha = digest(executable)
    if any(item["executable_sha256"] != executable_sha for item in base["runtime_evidence"]):
        raise ValueError("navigation evidence refers to a different executable")
    for name, expected in base["file_sha256"].items():
        if ledger.fingerprint(ledger.ROOT / name) != expected:
            raise ValueError("record fresh implementation hashes before navigation evidence")
    receipt = {"revision":ledger.REVISION, "status":"in_progress", "claims":[],
        "milestone_2_complete":False, "tutorial_complete":False,
        "scope":"Thirteen retail ordered-waypath inventories and malformed-byte fixtures; generic cardinal/SoA traversal; source human controls, collision blocking/resume, priority and completion fixtures. Retail NPC hull/locomotion preparation is exercised by the ordinary loaded scenes. Separate partial course evidence covers natural Logan jump/EVA route completion after explicit player trigger placement; no complete tutorial playthrough is established.",
        "source_references":["Code/wwphys/waypath.cpp", "Code/wwphys/waypoint.cpp", "Code/wwphys/Path.cpp",
                             "Code/wwmath/cardinalspline.cpp", "Code/wwmath/hermitespline.cpp", "Code/Combat/action.cpp",
                             "Code/Combat/soldier.cpp", "Code/Scripts/Mission00.cpp", "Code/Scripts/Mission00.h"],
        "passed_tests":sorted(units|runtime), "file_sha256":base["file_sha256"], "hash_policy":base["hash_policy"],
        "result_sha256":base["result_sha256"], "executable_sha256":executable_sha,
        "limitations":["Saved player startup/input permission and the jump-zone route/relocation/facing/camera/dialogue sequence have separate partial receipts. Speaking/camera/action arbitration, player course traversal and later tutorial progression remain unfinished; placement fixtures are not a mission playthrough.",
            "Loop padding, action portals/ladders, splined routes, destination solving, neighbor avoidance, no-progress failures and complete action arbitration remain open. Unsupported requests remain pending and do not complete.",
            "Skeleton interpolation weights decode with their original sign; morph rendering remains open.",
            "The course harness naturally completes two Logan routes through retail terrain collision after placing the player inside the jump trigger. That narrow setup does not establish arbitrary AI navigation or non-Windows graphics support."]}
    (ledger.ROOT / RECEIPT).write_text(json.dumps(receipt,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    rows = ledger.load_rows(ledger.MIGRATION)
    for row in rows:
        if row["id"] in {"behavior:ai.navigation", "behavior:campaign.ai.action_navigation"}:
            row.update(status="in_progress", target_paths=TARGETS, tests=TESTS, verification=RECEIPT)
            increment = " Current increment: Retail ordered 3D routes, shared cardinal/SoA target traversal, source soldier control/collision/priority fixtures and worker-count determinism. Live lesson progression, destination solving, looping/action routes, avoidance and source failure cases remain unverified."
            if increment not in row["notes"]: row["notes"] += increment
    ledger.write_rows(rows)
    print("Recorded partial navigation evidence. AI, tutorial and milestone 2 remain incomplete.")

if __name__ == "__main__": main()
