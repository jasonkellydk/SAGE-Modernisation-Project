#!/usr/bin/env python3
"""Migration ledger for the GeneralsMD -> ECS port.

The inventory is generated from the legacy source (read-only), so nothing can
be silently skipped: every registered module, INI block, INI field, script
action, script condition and engine subsystem gets exactly one ledger row.

  ledger.py sync     add rows for new legacy items, keep all edits, rewrite SUMMARY.md
  ledger.py check    CTest gate: inventory complete, rows valid, claims backed by files
  ledger.py summary  print the status table
"""

from __future__ import annotations

import argparse
import csv
import io
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

COLUMNS = ["id", "kind", "feature", "legacy_source", "placement", "target_paths",
           "tests", "status", "verification", "milestone", "notes"]
STATUSES = ["not_started", "in_progress", "ported", "verified", "not_applicable"]
# One CSV per kind keeps review diffs small; families are hand-maintained.
KIND_FILES = {
    "module": "modules.csv",
    "ini_block": "ini_blocks.csv",
    "ini_field": "ini_fields.csv",
    "script_action": "script_actions.csv",
    "script_condition": "script_conditions.csv",
    "subsystem": "subsystems.csv",
    "family": "families.csv",
}
GENERATED_KINDS = [kind for kind in KIND_FILES if kind != "family"]
PLACEMENT = re.compile(r"^(engine(/[a-z0-9_-]+)+|games/generalszh(/[a-z0-9_-]+)*)$")
LEGACY_ROOTS = ("GeneralsMD/", "Core/")

SCAN_DIRS = [
    "GeneralsMD/Code/GameEngine",
    "GeneralsMD/Code/GameEngineDevice",
    "Core/GameEngine",
    "Core/GameEngineDevice",
]
MODULE_FACTORIES = [
    "GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp",
    "Core/GameEngineDevice/Source/W3DDevice/Common/Thing/W3DModuleFactory.cpp",
]
SCRIPTS_HEADER = "GeneralsMD/Code/GameEngine/Include/GameLogic/Scripts.h"
INI_TYPE_TABLE = "Core/GameEngine/Source/Common/INI/INI.cpp"
GAME_ENGINE = "GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp"


@dataclass
class Item:
    id: str
    kind: str
    feature: str
    sources: list[str] = field(default_factory=list)


# ---------------------------------------------------------------- legacy scan

_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)


def strip_comments(text: str) -> str:
    """Remove comments but keep string literals and line numbering."""
    def keep(match: re.Match[str]) -> str:
        token = match.group(0)
        if token.startswith("/"):
            return "\n" * token.count("\n") + " "
        return token
    return _TOKEN.sub(keep, text)


def read(root: Path, rel: str) -> str:
    return strip_comments((root / rel).read_text(encoding="latin-1"))


def enum_members(text: str, enum_name: str) -> list[str]:
    match = re.search(r"enum\s+" + enum_name + r"\s*\{(.*?)\}", text, re.S)
    if not match:
        raise SystemExit(f"ledger: enum {enum_name} not found")
    names = []
    for entry in match.group(1).split(","):
        name = entry.split("=")[0].strip()
        if name and name != "NUM_ITEMS" and re.fullmatch(r"[A-Za-z_]\w*", name):
            names.append(name)
    return names


_TABLE = re.compile(r"FieldParse\s+([A-Za-z_][\w:]*)\s*\[\s*\]\s*=\s*\{")
_OWNER = re.compile(r"(?:\b(?:class|struct)\s+([A-Za-z_]\w*)\s*(?::[^;{]*)?\{)|(?:\b([A-Za-z_]\w*)::[A-Za-z_~]\w*\s*\([^;{]*\)\s*(?:const\s*)?\{)")


def table_owner(text: str, start: int, table: str) -> str:
    if "::" in table:
        return table.split("::")[0]
    owner = None
    for match in _OWNER.finditer(text, 0, start):
        owner = match.group(1) or match.group(2)
    return owner or table


def table_body(text: str, open_brace: int) -> str:
    depth = 0
    index = open_brace
    while index < len(text):
        character = text[index]
        if character == '"':
            # Skip string literals so braces inside names don't count.
            index += 1
            while index < len(text) and text[index] != '"':
                index += 2 if text[index] == "\\" else 1
        elif character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:index]
        index += 1
    raise SystemExit("ledger: unbalanced FieldParse table")


def scan_legacy(root: Path) -> dict[str, Item]:
    items: dict[str, Item] = {}

    def add(kind: str, key: str, feature: str, source: str) -> None:
        item_id = f"{kind}:{key}"
        item = items.setdefault(item_id, Item(item_id, kind, feature))
        if source not in item.sources:
            item.sources.append(source)

    for rel in MODULE_FACTORIES:
        for name in re.findall(r"addModule\s*\(\s*(\w+)\s*\)", read(root, rel)):
            add("module", name, name, rel)

    scripts = read(root, SCRIPTS_HEADER)
    for name in enum_members(scripts, "ScriptActionType"):
        add("script_action", name, name, SCRIPTS_HEADER)
    for name in enum_members(scripts, "ConditionType"):
        add("script_condition", name, name, SCRIPTS_HEADER)

    type_table = read(root, INI_TYPE_TABLE)
    body = re.search(r"theTypeTable\s*\[\s*\]\s*=\s*\{(.*?)\n\s*\};", type_table, re.S)
    for name in re.findall(r'\{\s*"(\w+)"', body.group(1) if body else ""):
        add("ini_block", name, name, INI_TYPE_TABLE)

    engine = read(root, GAME_ENGINE)
    for name in re.findall(r'initSubsystem\s*\(\s*\w+\s*,\s*"(\w+)"', engine):
        add("subsystem", name, name, GAME_ENGINE)

    for directory in SCAN_DIRS:
        for path in sorted((root / directory).rglob("*")):
            if path.suffix.lower() not in (".h", ".cpp") or not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            text = strip_comments(path.read_text(encoding="latin-1"))
            for match in _TABLE.finditer(text):
                owner = table_owner(text, match.start(), match.group(1))
                for field_name in re.findall(r'\{\s*"([^"]+)"', table_body(text, match.end() - 1)):
                    add("ini_field", f"{owner}.{field_name}", f"{owner} {field_name}", rel)
    return items


# ---------------------------------------------------------------- ledger io

def load(ledger_dir: Path) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    for kind, name in KIND_FILES.items():
        path = ledger_dir / name
        if not path.exists():
            continue
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            if reader.fieldnames != COLUMNS:
                raise SystemExit(f"ledger: {name} header must be {','.join(COLUMNS)}")
            for row in reader:
                if row["id"] in rows:
                    raise SystemExit(f"ledger: duplicate id {row['id']}")
                rows[row["id"]] = row
    return rows


def render(rows: list[dict[str, str]]) -> str:
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, fieldnames=COLUMNS, lineterminator="\n")
    writer.writeheader()
    for row in sorted(rows, key=lambda r: r["id"].lower()):
        writer.writerow(row)
    return buffer.getvalue()


def summary(rows: dict[str, dict[str, str]]) -> str:
    counts: dict[str, Counter[str]] = defaultdict(Counter)
    for row in rows.values():
        counts[row["kind"]][row["status"]] += 1
    lines = ["# Migration ledger summary", "",
             "Generated by `games/generalszh/migration/tools/ledger.py sync`. Do not edit.", "",
             "| kind | " + " | ".join(STATUSES) + " | total | done |",
             "|---|" + "---:|" * (len(STATUSES) + 2)]
    total: Counter[str] = Counter()
    for kind in KIND_FILES:
        count = counts.get(kind, Counter())
        total.update(count)
        lines.append(_summary_line(kind, count))
    lines.append(_summary_line("**all**", total))
    lines += ["", "`done` = ported + verified + not_applicable, over all rows.", ""]
    return "\n".join(lines)


def _summary_line(label: str, count: Counter[str]) -> str:
    rows = sum(count.values())
    done = count["ported"] + count["verified"] + count["not_applicable"]
    percent = f"{100 * done / rows:.1f}%" if rows else "-"
    return f"| {label} | " + " | ".join(str(count[s]) for s in STATUSES) + f" | {rows} | {percent} |"


def expected_files(rows: dict[str, dict[str, str]]) -> dict[str, str]:
    files = {}
    for kind, name in KIND_FILES.items():
        files[name] = render([row for row in rows.values() if row["kind"] == kind])
    files["SUMMARY.md"] = summary(rows)
    return files


# ---------------------------------------------------------------- commands

def merged(root: Path, ledger_dir: Path) -> tuple[dict[str, dict[str, str]], list[str]]:
    """Existing rows plus new inventory rows; returns (rows, stale ids)."""
    rows = load(ledger_dir)
    inventory = scan_legacy(root)
    for item in inventory.values():
        row = rows.get(item.id)
        if row is None:
            rows[item.id] = {column: "" for column in COLUMNS} | {
                "id": item.id, "kind": item.kind, "feature": item.feature, "status": "not_started"}
            row = rows[item.id]
        # Sources are derived, never hand-edited.
        row["legacy_source"] = ";".join(item.sources)
    stale = [row_id for row_id, row in rows.items()
             if row["kind"] in GENERATED_KINDS and row_id not in inventory]
    return rows, stale


def validate(root: Path, rows: dict[str, dict[str, str]]) -> list[str]:
    errors = []
    for row_id, row in sorted(rows.items()):
        def fail(message: str) -> None:
            errors.append(f"{row_id}: {message}")

        status = row["status"]
        if row["kind"] not in KIND_FILES:
            fail(f"unknown kind '{row['kind']}'")
        if status not in STATUSES:
            fail(f"unknown status '{status}'")
            continue
        placements = [p for p in row["placement"].split(";") if p]
        for placement in placements:
            if not PLACEMENT.match(placement):
                fail(f"placement '{placement}' must be an engine/... or games/generalszh/... path")
        targets = [p for p in row["target_paths"].split(";") if p]
        tests = [p for p in row["tests"].split(";") if p]
        for path in targets + tests:
            if path.startswith(LEGACY_ROOTS):
                fail(f"'{path}' points into legacy code; targets must be modern")
        if status == "not_applicable" and not row["notes"].strip():
            fail("not_applicable needs a reason in notes")
        if status in ("ported", "verified"):
            if not placements:
                fail(f"{status} needs a placement")
            if not targets:
                fail(f"{status} needs target_paths")
            if not tests:
                fail(f"{status} needs tests")
            for path in targets + tests:
                if not (root / path).exists():
                    fail(f"'{path}' does not exist")
            for path in targets:
                if placements and not any(path == p or path.startswith(p + "/") for p in placements):
                    fail(f"target '{path}' is outside placement {row['placement']}")
        if status == "verified" and not row["verification"].strip():
            fail("verified needs a verification note (behaviour test, parity capture, ...)")
    return errors


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["sync", "check", "summary"])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--ledger", type=Path, default=None)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    ledger_dir = (args.ledger or root / "games/generalszh/migration/ledger").resolve()

    rows, stale = merged(root, ledger_dir)
    if args.command == "summary":
        print(summary(rows))
        return 0
    if args.command == "sync":
        for row_id in stale:
            print(f"ledger: {row_id} no longer exists in legacy; review and delete it by hand", file=sys.stderr)
        ledger_dir.mkdir(parents=True, exist_ok=True)
        for name, content in expected_files(rows).items():
            (ledger_dir / name).write_text(content, encoding="utf-8", newline="\n")
        print(summary(rows))
        return 1 if stale else 0

    errors = validate(root, rows)
    errors += [f"{row_id}: stale, not in legacy source" for row_id in stale]
    for name, content in expected_files(rows).items():
        path = ledger_dir / name
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            errors.append(f"{name}: out of date; run `python games/generalszh/migration/tools/ledger.py sync`")
    if errors:
        print("Migration ledger check failed:\n  " + "\n  ".join(errors), file=sys.stderr)
        return 1
    print(f"Migration ledger OK: {len(rows)} rows")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
