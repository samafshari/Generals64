"""Build-step (VK configs only): compile every .hlsl entry in the shader
manifest to a .spv blob via dxc. Output lands next to the exe so the Vulkan
runtime's fopen("Shaders/spirv/...") resolves at launch.

This is the MSBuild-equivalent of what Renderer/Shaders/compile_shaders.cmake
does for the CMake path. The entry list here must stay in sync with the
SHADER_ENTRIES list in that .cmake file — if you add a new shader entry
point, edit both.

Usage:
  python compile_spirv.py --hlsl-dir <...> --out-dir <...> [--dxc <path>]

dxc.exe is resolved in order from:
  1. --dxc explicit path
  2. $(VULKAN_SDK)/Bin/dxc.exe
  3. PATH
"""

from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys


# (hlsl basename without ext, vertex/compute entry, pixel entry or None)
# Keep in sync with Renderer/Shaders/compile_shaders.cmake SHADER_ENTRIES.
ENTRIES: list[tuple[str, str, str | None]] = [
    # Shader3D — main 3D shader, many entry points.
    ("Shader3D", "VSMain",         "PSMain"),
    ("Shader3D", "VSMain",         "PSMainUnlit"),
    ("Shader3D", "VSMain",         "PSLaserGlow"),
    ("Shader3D", "VSMain",         "PSMainAlphaTest"),
    ("Shader3D", "VSMain",         "PSMeshDecal"),
    ("Shader3D", "VSMain",         "PSMainAlphaTestEdge"),
    ("Shader3D", "VSMain",         "PSMainSmudge"),
    ("Shader3D", "VSMain",         "PSGhost"),
    ("Shader3D", "VSMainTwoTex",   "PSMainTerrainMaskBase"),
    ("Shader3D", "VSMainWater",    "PSMainWaterBump"),
    ("Shader3D", "VSMainSkybox",   "PSMainSkybox"),
    # 2D shader
    ("Shader2D", "VSMain",         "PSMainColor"),
    ("Shader2D", "VSMain",         "PSMainTextured"),
    ("Shader2D", "VSMain",         "PSMainGrayscale"),
    # Snow
    ("ShaderSnow", "VSSnow",       "PSSnow"),
    # Terrain decals
    ("ShaderDecal", "VSDecal",     "PSDecal"),
    # Post-processing
    ("ShaderPost", "VSPost",       "PSBloomExtract"),
    ("ShaderPost", "VSPost",       "PSBlur"),
    ("ShaderPost", "VSPost",       "PSComposite"),
    # FSR
    ("ShaderFSR", "VSPost",        "PSEASU"),
    ("ShaderFSR", "VSPost",        "PSRCAS"),
    # Particle FX
    ("ShaderParticleFX", "VSPost", "PSParticleExtract"),
    ("ShaderParticleFX", "VSPost", "PSHeatDistort"),
    ("ShaderParticleFX", "VSPost", "PSGlowComposite"),
    # Shockwave
    ("ShaderShockwave", "VSPost",  "PSShockwave"),
    # God rays
    ("ShaderGodRays", "VSPost",    "PSGodRayExtract"),
    ("ShaderGodRays", "VSPost",    "PSGodRayBlur"),
    ("ShaderGodRays", "VSPost",    "PSGodRayComposite"),
    # Cinematic
    ("ShaderCinematic", "VSPost",  "PSCinematic"),
    # Film grain
    ("ShaderFilmGrain", "VSPost",  "PSFilmGrain"),
    # Sharpen
    ("ShaderSharpen", "VSPost",    "PSSharpen"),
    # Tilt shift
    ("ShaderTiltShift", "VSPost",  "PSTiltShift"),
    # Volumetric
    ("ShaderVolumetric", "VSPost", "PSVolumetric"),
    # Lens flare
    ("ShaderLensFlare", "VSPost",  "PSLensFlare"),
    # Debug line overlays
    ("ShaderDebug", "VSMain",      "PSMain"),
    # GPU particles (compute + render)
    ("GPUParticleUpdate", "CSUpdate",   None),
    ("GPUParticleRender", "VSParticle", "PSParticle"),
]


def find_dxc(explicit: str | None) -> pathlib.Path:
    if explicit:
        p = pathlib.Path(explicit)
        if p.is_file():
            return p
        raise FileNotFoundError(f"--dxc path does not exist: {explicit}")

    vk_sdk = os.environ.get("VULKAN_SDK")
    if vk_sdk:
        p = pathlib.Path(vk_sdk) / "Bin" / "dxc.exe"
        if p.is_file():
            return p

    # Fall back to PATH
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = pathlib.Path(d) / "dxc.exe"
        if p.is_file():
            return p

    raise FileNotFoundError("dxc.exe not found; set VULKAN_SDK or pass --dxc")


def newer(src: pathlib.Path, dst: pathlib.Path) -> bool:
    return (not dst.exists()) or src.stat().st_mtime > dst.stat().st_mtime


def compile_entry(
    dxc: pathlib.Path,
    hlsl_path: pathlib.Path,
    entry: str,
    profile: str,
    out_spv: pathlib.Path,
) -> bool:
    cmd = [
        str(dxc),
        "-T", profile,
        "-E", entry,
        "-spirv",
        "-fvk-b-shift", "0",  "0",
        "-fvk-t-shift", "10", "0",
        "-fvk-s-shift", "20", "0",
        "-fvk-u-shift", "30", "0",
        "-fspv-target-env=vulkan1.0",
        str(hlsl_path),
        "-Fo", str(out_spv),
    ]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError as e:
        print(f"dxc invocation failed: {e}", file=sys.stderr)
        return False
    if r.returncode != 0:
        print(f"dxc FAILED for {hlsl_path.name} [{entry}] ({profile}):", file=sys.stderr)
        if r.stdout:
            print(r.stdout, file=sys.stderr)
        if r.stderr:
            print(r.stderr, file=sys.stderr)
        return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--hlsl-dir", type=pathlib.Path, default=None)
    ap.add_argument("--out-dir",  type=pathlib.Path, required=True)
    ap.add_argument("--dxc",      type=str, default=None)
    ap.add_argument("--force",    action="store_true")
    args = ap.parse_args()

    if args.hlsl_dir is None:
        args.hlsl_dir = pathlib.Path(__file__).resolve().parents[1] / "VulkanHLSL"

    if not args.hlsl_dir.is_dir():
        print(f"error: {args.hlsl_dir} not a directory", file=sys.stderr)
        return 1

    try:
        dxc = find_dxc(args.dxc)
    except FileNotFoundError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    args.out_dir.mkdir(parents=True, exist_ok=True)

    failed = 0
    compiled = 0
    skipped = 0
    for base, vs_entry, ps_entry in ENTRIES:
        hlsl = args.hlsl_dir / f"{base}.hlsl"
        if not hlsl.is_file():
            print(f"error: missing {hlsl}", file=sys.stderr)
            failed += 1
            continue

        # Vertex / compute shader (one per entry; detect by name prefix).
        v_profile = "cs_6_0" if vs_entry.startswith("CS") else "vs_6_0"
        v_spv = args.out_dir / f"{base}_{vs_entry}.spv"
        if args.force or newer(hlsl, v_spv):
            if compile_entry(dxc, hlsl, vs_entry, v_profile, v_spv):
                compiled += 1
            else:
                failed += 1
        else:
            skipped += 1

        # Pixel shader, if present
        if ps_entry:
            p_spv = args.out_dir / f"{base}_{ps_entry}.spv"
            if args.force or newer(hlsl, p_spv):
                if compile_entry(dxc, hlsl, ps_entry, "ps_6_0", p_spv):
                    compiled += 1
                else:
                    failed += 1
            else:
                skipped += 1

    total = compiled + skipped + failed
    print(f"compile_spirv: {compiled} compiled, {skipped} up-to-date, {failed} failed ({total} total, out_dir={args.out_dir})", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
