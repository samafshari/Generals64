"""Build-step: turn every `<name>.hlsl` in VulkanHLSL/ into `<name>.hlsl.inc`
containing the source wrapped in raw-string literal(s). ShaderSource.h then
#includes the .inc file as the initializer for `static const char* g_shaderX`.

Splitting: MSVC accepts C++ raw string literals up to 65535 bytes each, but
the concatenated-string-literal limit across multiple tokens is generous.
We split at ~15000 bytes at line boundaries so even very large shaders (like
Shader3D.hlsl at ~33 KB) emit cleanly. Adjacent R"HLSL(...)HLSL" literals
concatenate automatically per the C++ grammar.

Incremental: a .inc is regenerated only if the .hlsl has a newer mtime. Both
MSBuild's PreBuildEvent and CMake's add_custom_command call us every build;
the mtime check keeps it cheap.

Usage:  python generate_inc.py [hlsl-dir]

`hlsl-dir` defaults to Renderer/Shaders/VulkanHLSL relative to this script.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import sys


CHUNK_BYTES = 15000  # target size per R"HLSL(...)HLSL" literal
DELIMITER = "HLSL"   # matches what the rest of the codebase uses


def split_at_newline(text: str, target: int) -> list[str]:
    """Split `text` into chunks <= `target` bytes, breaking only on newlines.
    If a single line is longer than `target`, that line becomes its own chunk
    (oversized — MSVC will emit a warning but still compile up to 65535)."""
    if len(text) <= target:
        return [text]

    chunks: list[str] = []
    remaining = text
    while len(remaining) > target:
        split_idx = remaining.rfind("\n", 0, target + 1)
        if split_idx == -1 or split_idx == 0:
            # No newline in the first `target` bytes — fall through and take
            # one whole line, however long it is. The next iteration will see
            # a shorter remainder.
            split_idx = remaining.find("\n")
            if split_idx == -1:
                chunks.append(remaining)
                return chunks
        chunks.append(remaining[:split_idx + 1])
        remaining = remaining[split_idx + 1:]
    if remaining:
        chunks.append(remaining)
    return chunks


def should_regenerate(src: pathlib.Path, dst: pathlib.Path) -> bool:
    if not dst.exists():
        return True
    return src.stat().st_mtime > dst.stat().st_mtime


def generate_one(hlsl: pathlib.Path, out_inc: pathlib.Path) -> None:
    content = hlsl.read_text(encoding="utf-8")
    chunks = split_at_newline(content, CHUNK_BYTES)

    # Sanity check — our HLSL never contains the literal `)HLSL"` sequence
    # in practice, but if it did, the raw string would terminate early.
    for i, c in enumerate(chunks):
        if f"){DELIMITER}\"" in c:
            raise ValueError(f"{hlsl}: chunk {i} contains the raw-string terminator token; "
                             f"rename DELIMITER or escape the source")

    lines: list[str] = []
    for c in chunks:
        # Raw-string literals preserve newlines verbatim. We emit one per line
        # for diff friendliness: opening delimiter on its own line, content,
        # closing delimiter on its own line.
        if not c.endswith("\n"):
            c += "\n"
        lines.append(f'R"{DELIMITER}(\n{c}){DELIMITER}"')

    # Concatenate adjacent literals with a newline between. The compiler
    # treats them as a single string. An extra blank line at EOF makes the
    # `#include` → `;` transition in ShaderSource.h read naturally.
    body = "\n".join(lines) + "\n"
    out_inc.parent.mkdir(parents=True, exist_ok=True)
    out_inc.write_text(body, encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("hlsl_dir", nargs="?", type=pathlib.Path, default=None)
    ap.add_argument("--force", action="store_true", help="regenerate even if up-to-date")
    args = ap.parse_args()

    if args.hlsl_dir is None:
        args.hlsl_dir = pathlib.Path(__file__).resolve().parents[1] / "VulkanHLSL"

    if not args.hlsl_dir.is_dir():
        print(f"error: {args.hlsl_dir} is not a directory", file=sys.stderr)
        return 1

    hlsl_files = sorted(args.hlsl_dir.glob("*.hlsl"))
    if not hlsl_files:
        print(f"warning: no .hlsl files found in {args.hlsl_dir}", file=sys.stderr)
        return 0

    regenerated = 0
    for h in hlsl_files:
        inc = h.with_suffix(".hlsl.inc")
        if args.force or should_regenerate(h, inc):
            try:
                generate_one(h, inc)
            except ValueError as e:
                print(f"error: {e}", file=sys.stderr)
                return 1
            regenerated += 1

    if regenerated:
        print(f"generate_inc: regenerated {regenerated} of {len(hlsl_files)} shader(s)", file=sys.stderr)
    else:
        print(f"generate_inc: {len(hlsl_files)} shader(s) up to date", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
