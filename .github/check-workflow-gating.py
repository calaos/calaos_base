#!/usr/bin/env python3
"""Refuse a workflow_run gate that cannot fire, or that fires on red.

Run it from the repository root:  python3 .github/check-workflow-gating.py

The three refusals below are the ways a workflow_run gate breaks WITHOUT ever
producing an error: GitHub matches the quoted workflow by its `name:`, and an
unmatched name simply never triggers. The self-test runs first, on the same
functions as the real sweep, so a broken checker cannot accuse the tree.
"""

import pathlib
import sys

import yaml

# PyYAML follows YAML 1.1, where the bare key `on` is the boolean True.
ON_KEYS = ("on", True)


def workflow_on(doc):
    for k in ON_KEYS:
        if isinstance(doc, dict) and k in doc:
            return doc[k]
    return None


def as_trigger_map(on):
    """`on:` accepts a mapping, a bare string or a list of event names."""
    if isinstance(on, dict):
        return on
    if isinstance(on, str):
        return {on: None}
    if isinstance(on, list):
        return {e: None for e in on if isinstance(e, str)}
    return {}


def declared_name(path, doc):
    """The name GitHub matches on. Absent, it falls back to the file path."""
    if isinstance(doc, dict) and isinstance(doc.get("name"), str):
        return doc["name"]
    return str(path)


def scan(files):
    """files: {path: text}. Returns the list of complaints, in file order."""
    problems = []
    parsed = {}

    for path in sorted(files):
        try:
            doc = yaml.safe_load(files[path])
        except yaml.YAMLError as exc:
            problems.append(f"{path}: not valid YAML: {exc.__class__.__name__}")
            continue
        if not isinstance(doc, dict):
            problems.append(f"{path}: top level is not a mapping")
            continue
        if workflow_on(doc) is None:
            problems.append(f"{path}: no `on:` trigger")
            continue
        if not isinstance(doc.get("jobs"), dict) or not doc["jobs"]:
            problems.append(f"{path}: no `jobs:`")
            continue
        parsed[path] = doc

    by_name = {declared_name(p, d): p for p, d in parsed.items()}

    for path, doc in parsed.items():
        triggers = as_trigger_map(workflow_on(doc))
        wr = triggers.get("workflow_run")
        if not isinstance(wr, dict):
            continue

        cited = wr.get("workflows")
        if isinstance(cited, str):
            cited = [cited]
        if not cited:
            problems.append(f"{path}: workflow_run names no workflow")
            cited = []

        for name in cited:
            target = by_name.get(name)
            if target is None:
                near = ", ".join(sorted(by_name)) or "(none)"
                problems.append(
                    f"{path}: workflow_run cites {name!r}, which no workflow "
                    f"declares as its name: -- declared names are {near}")
                continue
            # A paths filter on the gating workflow means it can be skipped for a
            # whole class of commits. No run, no workflow_run event, no publish
            # -- and no failure anywhere to say so.
            for event, cfg in as_trigger_map(workflow_on(parsed[target])).items():
                if isinstance(cfg, dict) and (
                        "paths" in cfg or "paths-ignore" in cfg):
                    problems.append(
                        f"{path}: gating workflow {name!r} ({target}) filters "
                        f"{event} by paths; a filtered-out commit never "
                        f"triggers the gate and never publishes")

        # types: [completed] fires on failure and on cancellation too.
        for job_id, job in doc["jobs"].items():
            cond = job.get("if") if isinstance(job, dict) else None
            if not isinstance(cond, str) or "workflow_run.conclusion" not in cond:
                problems.append(
                    f"{path}: job {job_id!r} has no `if:` testing "
                    f"workflow_run.conclusion; workflow_run fires on a FAILED "
                    f"run as well, so this job publishes on red")

    return problems


# --------------------------------------------------------------------------
# Self-test corpus. Each entry is a whole workflow set, because every rule here
# is about one file citing another.

_CI = """
name: Build and Test
on:
  push:
  pull_request:
jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps: [ { run: make check } ]
"""

_PUB = """
name: Publish
on:
  workflow_run:
    workflows: [ "Build and Test" ]
    types: [ completed ]
    branches: [ master ]
jobs:
  publish:
    if: github.event.workflow_run.conclusion == 'success'
    runs-on: ubuntu-latest
    steps: [ { run: ./publish } ]
"""

REFUSED = {
    "cited name does not exist": {"ci.yml": _CI, "pub.yml": _PUB.replace(
        '"Build and Test"', '"Build and Tests"')},
    "cited by file name instead of name:": {"ci.yml": _CI, "pub.yml": _PUB.replace(
        '"Build and Test"', '"ci.yml"')},
    "cited name differs by case": {"ci.yml": _CI, "pub.yml": _PUB.replace(
        '"Build and Test"', '"build and test"')},
    "gating workflow declares no name:": {
        "ci.yml": _CI.replace("name: Build and Test\n", ""), "pub.yml": _PUB},
    "job has no if: at all": {"ci.yml": _CI, "pub.yml": _PUB.replace(
        "    if: github.event.workflow_run.conclusion == 'success'\n", "")},
    "if: never looks at the conclusion": {"ci.yml": _CI, "pub.yml": _PUB.replace(
        "github.event.workflow_run.conclusion == 'success'",
        "github.event.workflow_run.event == 'push'")},
    "gating workflow is filtered by paths": {
        "ci.yml": _CI.replace("  push:\n", "  push:\n    paths: [ 'src/**' ]\n"),
        "pub.yml": _PUB},
    "gating workflow is filtered by paths-ignore": {
        "ci.yml": _CI.replace("  push:\n", "  push:\n    paths-ignore: [ 'docs/**' ]\n"),
        "pub.yml": _PUB},
    "workflow_run names nothing": {"ci.yml": _CI, "pub.yml": _PUB.replace(
        '    workflows: [ "Build and Test" ]\n', "")},
    "the gating workflow is not valid YAML": {
        "ci.yml": _CI + "\n  bad:\n   - [unclosed\n", "pub.yml": _PUB},
}

ACCEPTED = {
    "the shape this repository ships": {"ci.yml": _CI, "pub.yml": _PUB},
    "no workflow_run anywhere": {"ci.yml": _CI},
    "if: spread over a folded scalar": {"ci.yml": _CI, "pub.yml": _PUB.replace(
        "    if: github.event.workflow_run.conclusion == 'success'\n",
        "    if: >-\n"
        "      github.event.workflow_run.conclusion == 'success' &&\n"
        "      github.event.workflow_run.head_sha == github.sha\n")},
    "two gated workflows on the same gate": {
        "ci.yml": _CI, "pub.yml": _PUB,
        "pub2.yml": _PUB.replace("name: Publish", "name: Publish again")},
    "the gating workflow filters by branch, not by path": {
        "ci.yml": _CI.replace("  push:\n", "  push:\n    branches: [ master ]\n"),
        "pub.yml": _PUB},
}


def self_test():
    failures = []
    for label, files in REFUSED.items():
        if not scan(files):
            failures.append(f"self-test: NOT refused: {label}")
    for label, files in ACCEPTED.items():
        got = scan(files)
        if got:
            failures.append(f"self-test: wrongly refused: {label}: {got}")
    for f in failures:
        print(f, file=sys.stderr)
    if failures:
        return False
    print(f"self-test ok: {len(REFUSED)} refused, {len(ACCEPTED)} accepted")
    return True


def main():
    if not self_test():
        return 2

    root = pathlib.Path(__file__).resolve().parent / "workflows"
    files = {}
    for p in sorted(root.iterdir()):
        if p.suffix in (".yml", ".yaml") and p.is_file():
            files[p.name] = p.read_text()
    if not files:
        print(f"no workflow found under {root}", file=sys.stderr)
        return 2

    problems = scan(files)
    for p in problems:
        print(f"FAIL {p}", file=sys.stderr)
    print(f"{len(files)} workflows scanned, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
