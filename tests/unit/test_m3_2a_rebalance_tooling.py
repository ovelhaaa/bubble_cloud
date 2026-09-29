from __future__ import annotations

import importlib.util
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "m3_2a_tonal_rebalance.py"

# The frozen historical baseline: the last commit before the M3.2A tonal bus
# rebalance. This value is asserted independently from the script constant so a
# typo or a moved default cannot pass.
FROZEN_M3_2A_BASELINE_SHA = "3b34b4d9718586cdd42060f5ea3e313a2ac9e8e5"


def _load_script_module():
    spec = importlib.util.spec_from_file_location("m3_2a_tonal_rebalance", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _resolve(ref: str) -> str:
    return subprocess.run(
        ["git", "rev-parse", f"{ref}^{{commit}}"],
        cwd=REPO_ROOT, capture_output=True, text=True, check=True,
    ).stdout.strip()


def test_default_baseline_is_the_frozen_pre_m3_2a_commit() -> None:
    """M3.2B.1: the default must be the pinned pre-M3.2A commit, not a dynamic
    parent. ``HEAD^``/``HEAD~N``/a merge-base only work until the next milestone
    commit and would silently compare two post-rebalance trees."""
    source = SCRIPT.read_text(encoding="utf-8")

    # The script constant and the CLI default must both be the frozen SHA.
    module = _load_script_module()
    assert module.M3_2A_BASELINE_SHA == FROZEN_M3_2A_BASELINE_SHA
    assert module.build_arg_parser().parse_args([]).baseline_ref == FROZEN_M3_2A_BASELINE_SHA

    # Dynamic defaults must not reappear.
    assert 'default="HEAD^"' not in source
    assert 'default="HEAD"' not in source
    assert 'default="HEAD~' not in source
    assert "default=M3_2A_BASELINE_SHA" in source

    # The pinned SHA must resolve to itself, be a real ancestor of HEAD and not
    # be HEAD itself (otherwise the A/B would compare the rebalance to itself).
    assert _resolve(FROZEN_M3_2A_BASELINE_SHA) == FROZEN_M3_2A_BASELINE_SHA
    assert _resolve("HEAD") != FROZEN_M3_2A_BASELINE_SHA
    is_ancestor = subprocess.run(
        ["git", "merge-base", "--is-ancestor", FROZEN_M3_2A_BASELINE_SHA, "HEAD"],
        cwd=REPO_ROOT, capture_output=True,
    )
    assert is_ancestor.returncode == 0, "pinned baseline is not an ancestor of HEAD"


def test_explicit_baseline_override_is_honored(tmp_path: Path) -> None:
    """An explicit ``--baseline-ref`` must win over the frozen default. Overriding
    to HEAD is used as the observable proof: the pinned default would never equal
    the candidate, so the abort can only come from the override."""
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
    assert (
        "Baseline and candidate resolve to the same commit" in result.stderr
    ), result.stderr
    assert "Choose an explicit --baseline-ref." in result.stderr
    assert not (tmp_path / "out").exists()


def test_report_records_full_provenance() -> None:
    """The M3.2A.1 protections must remain: full SHA resolution and provenance in
    both the CSV report and the console output."""
    source = SCRIPT.read_text(encoding="utf-8")
    assert "resolve_git_sha" in source
    assert "working_tree_dirty" in source
    for field in (
        "baseline_sha",
        "candidate_sha",
        "baseline_ref",
        "candidate_worktree_dirty",
        "generated_at_utc",
        "environment",
    ):
        assert field in source, f"provenance field {field} missing from report/console"


@pytest.mark.skipif(
    subprocess.run(
        ["git", "rev-parse", "--verify", "HEAD^"],
        cwd=REPO_ROOT, capture_output=True,
    ).returncode != 0,
    reason="repository has no parent commit to use as a baseline",
)
def test_pinned_baseline_differs_from_the_current_parent() -> None:
    """Guards the exact failure mode: after later commits, HEAD^ is no longer the
    pre-M3.2A baseline."""
    assert _resolve(FROZEN_M3_2A_BASELINE_SHA) != _resolve("HEAD^")
