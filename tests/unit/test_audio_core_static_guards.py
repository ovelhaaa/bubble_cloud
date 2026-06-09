from __future__ import annotations

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
AUDIO_CORE_PATHS = [
    REPO_ROOT / "core" / "dsp",
    REPO_ROOT / "core" / "engine",
    REPO_ROOT / "core" / "sound_bubbles_dsp.h",
]

BANNED_SYMBOLS = [
    # Heap allocation is not allowed in the realtime/core audio path.
    "malloc",
    "calloc",
    "realloc",
    "free",
    # Console/logging output is not allowed in the realtime/core audio path.
    "printf",
    # Locks/synchronization primitives are not allowed in the realtime/core audio path.
    "pthread_mutex_lock",
    "pthread_mutex_unlock",
    "pthread_mutex",
    "mtx_lock",
    "mtx_unlock",
    "mutex",
    "spinlock",
    "sem_wait",
    "sem_post",
    "flock",
    "lock",
    # File or OS I/O is not allowed in the realtime/core audio path.
    "fopen",
    "freopen",
    "fclose",
    "fread",
    "fwrite",
    "fprintf",
    "fputs",
    "puts",
    "open",
    "read",
    "write",
]


def _strip_c_comments_and_literals(source: str) -> str:
    """Remove comments and string/character literals before token scanning."""
    result: list[str] = []
    i = 0
    n = len(source)
    while i < n:
        if source.startswith("//", i):
            end = source.find("\n", i)
            if end == -1:
                break
            result.append("\n")
            i = end + 1
        elif source.startswith("/*", i):
            end = source.find("*/", i + 2)
            if end == -1:
                break
            result.append("\n" * source[i:end + 2].count("\n"))
            i = end + 2
        elif source[i] in {'"', "'"}:
            quote = source[i]
            result.append(" ")
            i += 1
            while i < n:
                if source[i] == "\\":
                    i += 2
                    continue
                if source[i] == quote:
                    i += 1
                    break
                if source[i] == "\n":
                    result.append("\n")
                else:
                    result.append(" ")
                i += 1
        else:
            result.append(source[i])
            i += 1
    return "".join(result)


def _iter_audio_core_sources() -> list[Path]:
    sources: list[Path] = []
    for path in AUDIO_CORE_PATHS:
        if path.is_file():
            sources.append(path)
        else:
            sources.extend(sorted(p for p in path.rglob("*") if p.suffix in {".c", ".h"}))
    return sources


def test_audio_core_realtime_path_has_no_heap_logging_locks_or_io() -> None:
    token_pattern = re.compile(r"\b(" + "|".join(re.escape(s) for s in BANNED_SYMBOLS) + r")\b")
    violations: list[str] = []

    for source_path in _iter_audio_core_sources():
        cleaned = _strip_c_comments_and_literals(source_path.read_text(encoding="utf-8"))
        for line_number, line in enumerate(cleaned.splitlines(), start=1):
            for match in token_pattern.finditer(line):
                rel = source_path.relative_to(REPO_ROOT)
                violations.append(f"{rel}:{line_number}: banned realtime-audio symbol {match.group(1)!r}")

    assert not violations, "Banned symbols found in the core audio path:\n" + "\n".join(violations)
