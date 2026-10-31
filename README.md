# RealOcean 0.33.5

Turnkey ocean plugin targeting Unreal Engine 5.8.2. Install the `RealOcean` folder under `<Project>/Plugins/` and reopen the editor. The project plugin is enabled by default; then press Play.

On editor startup the plugin owns and validates `/Game/RealOceanGenerated` and creates the required MPC, SingleLayerWater surface material/material instance, and underwater post-process material. Reserved-path assets without RealOcean ownership metadata are never modified. Cook/commandlet mode is validation-only and fails when generated content is missing or stale.

## Runtime model

- 3000 m physical depth contract with dual-spectrum swell + wind-sea waves.
- Analytic and optional persistent GPU foam, caustics, underwater optics, sun reflection/refraction, and elevation-driven day-cycle lighting.
- Server-authoritative multiplayer ocean clock/settings snapshot. Snapshot transport is explicitly versioned with `ProtocolVersion=1` and `SettingsSchemaVersion=1`; incompatible peers reject the snapshot instead of applying ambiguous settings.
- Automatic controller ownership/handoff for streaming worlds and join-in-progress clients.


## Visual optics polish

- `Turbidity` is now an active optical control. `0.10` is the neutral reference that preserves authored absorption/scattering coefficients; lower values reduce particulate scattering and higher values add bounded spectral particulate absorption/scattering. The same effective coefficients drive SingleLayerWater, underwater volume, CPU visibility/sun transmission and RDG/material caustics.
- Cinematic quality uses a deliberately low-energy finite-solar-disk glitter assist on top of SingleLayerWater. The assist is Fresnel-bounded, stronger at low sun, capped to avoid double-counting, and disabled in Performance/Balanced.
- Cinematic analytic foam history uses a temporal-stable world-space three-octave domain-warp breakup field. Older foam progressively loses coverage and density so retained streaks fragment instead of revealing a repeating two-sine pattern.

## Per-view rendering

The generated 0.33.5 material path is view-scoped. Native `CameraPositionWS` and camera-relative world position are used to rebuild wave/micro-wave phase anchors and camera water depth independently for every `FSceneView`. The world-global MPC now carries physical/configuration state, not the visual camera phase anchor.

| View type | Support | Notes |
| --- | --- | --- |
| Single local player | Supported | Full turnkey path. |
| Dynamic split-screen / multiple local players | Supported | Each view resolves wave phase, underwater depth and caustics independently. No primary-view fail-closed path. |
| SceneCapture2D / SceneCaptureCube surface rendering | Supported | Per-view wave phase is independent of the main player camera. |
| SceneCapture underwater post-process | Supported when the capture source/post-process path executes post processing | Stateless capture views intentionally do not reuse temporal caustics history. |
| RDG half-res caustics | Per-view | History is keyed by `View.State`; stateless captures run without temporal accumulation. |
| Persistent GPU foam | Shared world-space clipmap | All views sample the same physical field; views outside clipmap coverage fall back to analytic foam instead of sampling a wrong camera-relative field. |
| Stereo/VR | Architecture-compatible, not certified in this package | Uses native per-view material/view-extension inputs; target-device validation is still required. |

The unbound generated underwater material self-blends from per-view dynamic surface depth. It no longer uses the first player's global underwater blend to decide whether another view is underwater.

## Compatibility

| Item | Status |
| --- | --- |
| Unreal Engine 5.8.2 | Source/API target. Plugin descriptor is pinned to 5.8.2; project-specific UBT/UHT, shader and cook validation is still required. |
| Unreal Engine 5.8.0–5.8.1 | Not declared compatible. Rebuild/retest before use. |
| Unreal Engine 5.7.x and older | Not supported by this distribution. |
| Future UE 5.8 hotfixes / 5.9+ | Not assumed compatible; update descriptor only after UBT/UHT, material and shader validation. |
| Water plugin | Required and enabled by the plugin descriptor. |
| Source distribution | Yes. No precompiled platform binaries are included. |
| MPC contract | v12. Generated content rebuilds when its schema/sentinel is stale. |
| Shader API | v19. |
| Turnkey generated-material schema | v37. |
| Multiplayer snapshot protocol | v1. |
| Multiplayer settings schema | v1. |

This package does not claim platform certification merely from source compatibility. Run project-specific UBT/UHT, shader compilation, cook/package and target-hardware tests before shipping.

## Upgrade notes

Generated assets from older versions are rebuilt when they are RealOcean-owned and stale. Legacy untagged generated content from versions before ownership metadata must be moved or deleted once; RealOcean deliberately does not adopt untagged assets.

`RawDepthMeters` remains a deprecated compatibility alias for `DynamicSurfaceDepthMeters`; new gameplay code should use `MeanSeaDepthMeters` or `DynamicSurfaceDepthMeters` explicitly.

If RealOcean-owned generated content needs repair, run `RealOcean.RebuildTurnkeyContent` in the editor console.

Shipping GPU foam remains fail-closed unless separately target-hardware certified. The default analytic foam path does not require a certificate.
