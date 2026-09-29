from __future__ import annotations

import importlib.util
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "m3_2c_jitter_qualification.py"

# The frozen historical baseline: the last commit before M3.2C was merged.
FROZEN_M3_2C_BASELINE_SHA = "64c68456b06e6a3f0350f55d3703858beb418c66"


def _load_script_module():
    spec = importlib.util.spec_from_file_location("m3_2c_jitter_qualification", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _resolve(ref: str) -> str:
    return subprocess.run(
        ["git", "rev-parse", f"{ref}^{{commit}}"],
        cwd=REPO_ROOT, capture_output=True, text=True, check=True,
    ).stdout.strip()


def test_default_baseline_is_the_frozen_pre_m3_2c_commit() -> None:
    """The default baseline must be the pinned pre-M3.2C commit, not a dynamic parent."""
    source = SCRIPT.read_text(encoding="utf-8")

    module = _load_script_module()
    assert module.M3_2C_BASELINE_SHA == FROZEN_M3_2C_BASELINE_SHA
    assert module.build_arg_parser().parse_args([]).baseline_ref == FROZEN_M3_2C_BASELINE_SHA

    # Dynamic defaults must not be used
    assert 'default="HEAD^"' not in source
    assert 'default="HEAD"' not in source
    assert 'default="HEAD~' not in source
    assert "default=M3_2C_BASELINE_SHA" in source

    # Pinned SHA must resolve, be an ancestor of HEAD, and not be HEAD itself
    assert _resolve(FROZEN_M3_2C_BASELINE_SHA) == FROZEN_M3_2C_BASELINE_SHA
    assert _resolve("HEAD") != FROZEN_M3_2C_BASELINE_SHA
    is_ancestor = subprocess.run(
        ["git", "merge-base", "--is-ancestor", FROZEN_M3_2C_BASELINE_SHA, "HEAD"],
        cwd=REPO_ROOT, capture_output=True,
    )
    assert is_ancestor.returncode == 0, "pinned baseline is not an ancestor of HEAD"


def test_explicit_baseline_override_is_honored(tmp_path: Path) -> None:
    """An explicit --baseline-ref must win over the default, and overriding to HEAD aborts with code 2."""
    module = _load_script_module()
    assert module.build_arg_parser().parse_args(["--baseline-ref", "HEAD"]).baseline_ref == "HEAD"

    result = subprocess.run(
        [sys.executable, str(SCRIPT), "--baseline-ref", "HEAD", "--output-dir", str(tmp_path / "out")],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2, (
        f"expected abort exit code 2, got {result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "Baseline and candidate resolve to the same commit" in result.stderr
    assert "Choose an explicit --baseline-ref." in result.stderr
    assert not (tmp_path / "out").exists()


def test_report_records_full_provenance() -> None:
    """The script must record full provenance in its report and console."""
    source = SCRIPT.read_text(encoding="utf-8")
    assert "resolve_git_sha" in source
    assert "working_tree_dirty" in source
    for field in (
        "baseline_sha",
        "candidate_sha",
        "baseline_ref",
        "candidate_dirty",
        "generated_at",
        "environment",
    ):
        assert field in source, f"provenance field {field} missing from script"
