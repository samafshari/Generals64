"""One-shot migration: extract each g_shaderFoo raw-string literal from
ShaderSource.h (and friends) into Renderer/Shaders/VulkanHLSL/<name>.hlsl.
After this script runs, the .hlsl files are the authoritative source and
ShaderSource.h can be rewritten as thin #include directives.

Usage:  python extract_to_hlsl.py [source-headers...]

Writes files to:  <repo>/Renderer/Shaders/VulkanHLSL/<derived-name>.hlsl

The derivation maps `g_shaderX` -> `ShaderX.hlsl`, with explicit entries for
known names (e.g. `g_shader2D` -> `Shader2D.hlsl`, `g_shaderGPUParticleUpdate`
-> `GPUParticleUpdate.hlsl`).
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


# g_shader name -> output basename (without extension). Names missing here
# fall through to the default: strip leading "g_", capitalize. For the legacy
# lowercase tokens we override explicitly.
NAME_MAP: dict[str, str] = {
    "g_shader3D":            "Shader3D",
    "g_shader2D":            "Shader2D",
    "g_shaderSnow":          "ShaderSnow",
    "g_shaderDecal":         "ShaderDecal",
    "g_shaderPost":          "ShaderPost",
    "g_shaderFSR":           "ShaderFSR",
    "g_shaderParticleFX":    "ShaderParticleFX",
    "g_shaderShockwave":     "ShaderShockwave",
    "g_shaderGodRays":       "ShaderGodRays",
    "g_shaderCinematic":     "ShaderCinematic",
    "g_shaderVolumetric":    "ShaderVolumetric",
    "g_shaderLensFlare":     "ShaderLensFlare",
    "g_shaderFilmGrain":     "ShaderFilmGrain",
    "g_shaderSharpen":       "ShaderSharpen",
    "g_shaderTiltShift":     "ShaderTiltShift",
    "g_shaderShadowDepth":   "ShaderShadowDepth",
    "g_shaderDebug":         "ShaderDebug",
    "g_shaderGPUParticleUpdate": "GPUParticleUpdate",
    "g_shaderGPUParticleRender": "GPUParticleRender",
}


# Compute shaders don't cleanly map to a single .hlsl file (they're small
# helpers — csPathPoint, csLocoMath). Leave them as inline literals in
# ComputeShaderSource.h and skip extraction. Surface a warning if encountered.
SKIP_TOKENS: set[str] = {
    "g_csPathPoint",
    "g_csLocoMath",
}


SHADER_DEF_RE = re.compile(
    r"""^\s*static\s+const\s+char\s*\*\s*(?P<name>g_\w+)\s*=\s*""",
    re.MULTILINE,
)


def find_literal_blocks(text: str, start: int) -> tuple[str, int]:
    """Starting at `start` in `text`, scan through a series of R"HLSL(...)HLSL"
    literals separated only by whitespace/comments, ending at the terminating
    semicolon. Return (concatenated-contents, end-offset-after-semicolon).

    We specifically look for R"HLSL(…)HLSL" where the delimiter token is HLSL.
    Raw-string content may contain anything except the exact sequence )HLSL".
    """
    pos = start
    parts: list[str] = []
    while True:
        # Skip whitespace and line / block comments between literals.
        while pos < len(text):
            if text[pos].isspace():
                pos += 1
                continue
            if text[pos:pos + 2] == "//":
                eol = text.find("\n", pos)
                pos = eol + 1 if eol != -1 else len(text)
                continue
            if text[pos:pos + 2] == "/*":
                end = text.find("*/", pos + 2)
                if end == -1:
                    raise ValueError("unterminated block comment at offset %d" % pos)
                pos = end + 2
                continue
            break

        if pos >= len(text):
            raise ValueError("reached EOF without a closing ';' for literal group")

        # Terminator?
        if text[pos] == ";":
            return ("".join(parts), pos + 1)

        # Expect R"HLSL(
        if text[pos:pos + 7] != 'R"HLSL(':
            raise ValueError("expected R\"HLSL( at offset %d, found: %r" % (pos, text[pos:pos + 20]))

        pos += 7
        end = text.find(')HLSL"', pos)
        if end == -1:
            raise ValueError("unterminated raw string starting near offset %d" % (pos - 7))
        parts.append(text[pos:end])
        pos = end + 6  # len(')HLSL"')


def derive_basename(name: str) -> str | None:
    if name in NAME_MAP:
        return NAME_MAP[name]
    # Default: strip leading "g_" and capitalize first letter.
    if name.startswith("g_"):
        tail = name[2:]
        if not tail:
            return None
        return tail[0].upper() + tail[1:]
    return None


def extract(header_path: pathlib.Path, out_dir: pathlib.Path, dry_run: bool = False) -> list[pathlib.Path]:
    text = header_path.read_text(encoding="utf-8")
    written: list[pathlib.Path] = []

    for m in SHADER_DEF_RE.finditer(text):
        name = m.group("name")
        if name in SKIP_TOKENS:
            print(f"  skip {name} (compute helper, left inline)", file=sys.stderr)
            continue

        try:
            content, _end = find_literal_blocks(text, m.end())
        except ValueError as e:
            print(f"  FAILED to parse {name}: {e}", file=sys.stderr)
            continue

        basename = derive_basename(name)
        if basename is None:
            print(f"  skip {name} (no output mapping)", file=sys.stderr)
            continue

        out_path = out_dir / f"{basename}.hlsl"
        # Strip a single leading newline (the R"HLSL( is typically followed by
        # one) to avoid accumulating blank lines. Preserve everything else.
        if content.startswith("\n"):
            content = content[1:]
        # Ensure trailing newline for POSIX-friendly files.
        if not content.endswith("\n"):
            content += "\n"

        if dry_run:
            print(f"  would write {out_path} ({len(content)} bytes)")
        else:
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_text(content, encoding="utf-8", newline="\n")
            print(f"  wrote {out_path} ({len(content)} bytes)")
            written.append(out_path)

    return written


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("headers", nargs="*", type=pathlib.Path,
                    help="ShaderSource.h / GPUParticleShaders.h / ComputeShaderSource.h paths")
    ap.add_argument("--out", type=pathlib.Path, default=None,
                    help="Output directory (default: Renderer/Shaders/VulkanHLSL next to first header)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not args.headers:
        repo = pathlib.Path(__file__).resolve().parents[3]
        args.headers = [
            repo / "Renderer" / "Shaders" / "ShaderSource.h",
            repo / "Renderer" / "Shaders" / "GPUParticleShaders.h",
            repo / "Renderer" / "Shaders" / "ComputeShaderSource.h",
        ]

    out_dir = args.out or (args.headers[0].parent / "VulkanHLSL")

    for h in args.headers:
        if not h.exists():
            print(f"skip: {h} does not exist", file=sys.stderr)
            continue
        print(f"extracting from {h} -> {out_dir}", file=sys.stderr)
        extract(h, out_dir, dry_run=args.dry_run)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
