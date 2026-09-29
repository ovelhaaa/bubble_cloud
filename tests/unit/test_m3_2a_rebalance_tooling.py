from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "m3_2a_tonal_rebalance.py"


def test_baseline_ref_defaults_to_parent_commit() -> None:
    """M3.2A.1: once M3.2A is committed, HEAD would collapse onto the candidate.

    The default must therefore point one commit back, and the script must expose
    the full SHA provenance it resolved.
    """
    source = SCRIPT.read_text(encoding="utf-8")
    assert 'default="HEAD^"' in source
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


def test_identical_baseline_and_candidate_aborts_before_rendering(tmp_path: Path) -> None:
    """Passing a ref that resolves to the same commit as HEAD must abort with a
    clear message instead of silently rendering a null A/B. This runs before any
    compiler or worktree work, so it is fast and always available."""
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
    # Nothing should have been rendered to disk.
    assert not (tmp_path / "out").exists()


@pytest.mark.skipif(
    subprocess.run(
        ["git", "rev-parse", "--verify", "HEAD^"],
        cwd=REPO_ROOT, capture_output=True,
    ).returncode != 0,
    reason="repository has no parent commit to use as a baseline",
)
def test_baseline_and_candidate_resolve_to_distinct_shas() -> None:
    resolve = lambda ref: subprocess.run(  # noqa: E731
        ["git", "rev-parse", f"{ref}^{{commit}}"],
        cwd=REPO_ROOT, capture_output=True, text=True, check=True,
    ).stdout.strip()
    assert resolve("HEAD^") != resolve("HEAD")
