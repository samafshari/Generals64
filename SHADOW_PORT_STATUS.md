# Shadow Port Status — Autonomous Session

## Build
ReleasePublic (DX11) clean. `generalszh.exe` produced. Both shadow toggles
in `W3DDisplay.cpp` default to `false` (enabled):
- `g_debugDisableProjectedShadows` (line ~107)
- `g_debugDisableVolumetricShadows` (line ~108)

## CURRENT STATE — volumetric disabled by default after cap rewrite crashed

Projected shadows remain enabled and working. Volumetric shadows are turned
off (`g_debugDisableVolumetricShadows = true` in `W3DDisplay.cpp:108`) until
the crash-after-load is triaged. The cap-layout rewrite below still compiles
clean so it's likely a runtime size / null-deref issue in the buffer-write
path, not a build error.

## Latest fix (the big one, after user reported pillar-shaped shadows)

### Closed shadow volumes — fan caps on top + bottom
Both `constructVolumeVB` (static path) and `constructVolume` (dynamic path)
were emitting OPEN volumes — side-wall quads only, no top or bottom cap.
The original D3D8 code was identical but its particular ZPass / two-pass
pipeline happened to tolerate the open topology in the common case. Under
the DX11 port, camera rays passing through the volume's open bottom fired
`INCR` on entry through a side wall without a matching `DECR` on exit,
leaving stencil positive across every volume-interior pixel. The darken
pass then multiplied those pixels, drawing the whole volume as a grey
translucent pillar (very visible for airborne casters with tall volumes).

Rewrite emits a topologically closed volume:
- 4 non-shared verts per silhouette edge (silh_start, extrude_start,
  silh_end, extrude_end)
- 2 global fan centers (silhouette centroid + its extrusion)
- 4 triangles per edge: 2 side-wall + 1 top cap + 1 bottom cap
  (caps fan-triangulate from the centroid)

`allocateShadowVolume` now sizes the Geometry for `4N + 2` verts /
`4N` triangles instead of the original `2N` / `N` strip-shared layout.

## Fixes landed this session (in priority order by impact)

### 1. Volumetric world-matrix transpose (CRITICAL — explains "no shadows + air artifacts")
`W3DVolumetricShadow.cpp` ~line 963: `SetVolShadowUploadWorld` was
`memcpy(&oc.world, &Matrix4x4(meshXform))` — that produces a transposed
matrix vs what HLSL `mul(v, world)` expects. Switched to
`RenderUtils::Matrix3DToFloat4x4(*meshXform)` which bakes the transpose.
Matches the convention in `Shader3D.hlsl`.

### 2. Renderer object-cbuffer cache invalidation (CRITICAL — explains water not rendering)
`Renderer::Draw3D` caches `m_objectCBBound` to skip re-binding the object
cbuffer. Shadow code binds its own `worldCB` to VS@b1 directly, and the
cache stays `true` — next `Draw3D` doesn't rebind `m_cbObject`, reads
world=0 from shadow's cbuffer, water vertex positions collapse.

Added public `Renderer::InvalidateObjectCBCache()` and call it:
- In `Renderer::Restore3DState()` as a defensive reset (one extra rebind
  per state restore — negligible).
- Explicitly at end of `W3DVolumetricShadowManager::renderShadows()`.

### 3. Projected-shadow heightmap wiring (fixes projected path being a no-op)
`TheTerrainRenderObject` is always nullptr in this build. Switched
`W3DProjectedShadow.cpp` to `GetTerrainHeightMap()` (from D3D11Shims) at
both call sites: top of `renderShadows` and inside `queueDecal`.

### 4. First-map WRITE_DISCARD for dynamic shadow VB/IB
`W3DVolumetricShadow.cpp` `RenderDynamicMeshVolume`: was using
WRITE_NO_OVERWRITE for the first map each frame at cursor=0, racing the
GPU's previous-frame reads. Added a `g_dynamicFirstMap` flag reset at top
of `renderShadows` that forces DISCARD on the first map per frame.

### 5. Null guards + index bug (earlier in session)
- `RenderVolume` bails on null `m_robj` / `m_geometry` / out-of-range
  light/mesh indices / null `m_shadowVolume[light][mesh]`.
- `Update()` null-checks `m_robj` before `Get_Position()`.
- Fixed `m_shadowVolume[0][meshIndex]` → `[lightIndex][meshIndex]` in
  RenderVolume's SHADOW_DYNAMIC test.

## Known stubs (intentional — will not crash but miss features)

- **SHADOW_DYNAMIC_PROJECTION / SHADOW_DIRECTIONAL_PROJECTION**: fall
  through to the static decal path using cached silhouette texture.
  Real projection silhouette-generation pipeline isn't ported.
- **ZFail** (camera-inside-volume) branch is not taken. Always ZPass.
  Breaks when camera enters a shadow volume — visual artifacts possible
  up close.
- **Terrain raycast for extrusion padding**: `updateOptimalExtrusionPadding`
  uses a fixed `SHADOW_EXTRUSION_BUFFER` fallback. Cliff-edge shadows
  won't extend optimally until terrain raycast helper is hooked up.
- **Frustum-based volume culling**: not plumbed — all volumes are rendered
  every frame. Performance hit on large scenes, not correctness.
- **`renderProjectedTerrainShadow`**: returns 0 (stub). The original
  renders the projected silhouette onto a heightmap patch of receivers.
  Decal fallback covers terrain coverage, but buildings/infantry don't
  receive projected shadows yet.
- **`loadTerrainShadows`**: explicit no-op — `DO_TERRAIN_SHADOW_VOLUMES`
  was never shipped in the original either.

## Files touched this session

- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp` — matrix transpose, null guards, light-index bug, cbuffer cache invalidation, first-map DISCARD
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp` — heightmap wiring (GetTerrainHeightMap), related includes
- `Renderer/Renderer.h` — added `InvalidateObjectCBCache()` public method
- `Renderer/Renderer.cpp` — added `m_objectCBBound = false` to `Restore3DState`
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` — toggle defaults

## Next steps (for when you're back)

1. **Run the game**. If shadows still look wrong, report what — blob shapes, wrong position, flickering, stencil leakage onto non-shadow surfaces.
2. If shadows don't appear at all, check `TheGlobalData->m_useShadowVolumes` is TRUE at runtime. The LOD system forces this to TRUE on custom/very-high detail settings (`GameLOD.cpp:623`).
3. If water still breaks, inspect the DX11 debug layer output — look for cbuffer binding warnings.
4. If crash returns, the stack trace in `crash.log` / DEBUG_CRASH output will narrow it down.
5. Potential remaining issues I can only triage with runtime feedback:
   - Buffer-manager slot re-allocation race (WRITE_NO_OVERWRITE on recycled slot)
   - Shadow geometry cache outlives its source MeshClass on rare asset reloads
   - First-frame projection shadow texture loading (if ImageCache misses shadow.tga)
