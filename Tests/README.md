# Shadow-mapping test suite

Headless tests for the sun shadow-map pipeline. No D3D11 context, no engine,
no screenshots — catch math + cbuffer-layout bugs directly.

## Files

- `ShadowMathTests.cpp` — Pure-math tests. Exercises `RenderMath` primitives
  (`Float4x4LookAtRH`, `Float4x4OrthoRH`, `Float4x4Multiply`), the live
  `BuildSunViewProjection` logic, a C++ reimplementation of the HLSL
  `ComputeShadowVisibility` path, and a transpose-probe that reproduces the
  exact symptom of a column-major read of a row-major matrix.

- `ShaderReflectionTest.cpp` — Compiles `Shader3D.hlsl` with `D3DCompile` and
  walks the reflection API to verify `FrameConstants` cbuffer offsets and
  matrix-class layout. Catches cbuffer-layout drift between C++ and HLSL.

## Build + run

```
cl /std:c++17 /EHsc /nologo Tests/ShadowMathTests.cpp
./ShadowMathTests.exe

cl /std:c++17 /EHsc /nologo Tests/ShaderReflectionTest.cpp d3dcompiler.lib dxguid.lib
./ShaderReflectionTest.exe
```

Exit code = number of failed assertions. Silence = all green.

## What the suite verified

- `BuildSunViewProjection` depends on camera XY only — invariant against
  camera elevation, pitch, yaw, and roll.
- Same world point projects to the same UV + depth whether it enters the
  shadow map as a caster or samples the shadow map as a receiver.
- Focus point lands at UV (0.5, 0.5).
- Higher world-Z → smaller receiver depth (closer to sun).
- End-to-end occlusion: a pillar correctly shadows the ground at the
  offset predicted by the sun ray and self-shadows its own base.
- `FrameConstants` cbuffer has `sunViewProjection` at offset 368 with
  MATRIX_ROWS class in the compiled shader bytecode.
- Transposed matrix produces the "shadow plane moves with camera" symptom
  — an early-warning test so any future regression that silently flips the
  matrix layout is caught before visual debugging is needed.
