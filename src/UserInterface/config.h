#pragma once

// Centralized runtime tuning for DX11 migration.
// Keep this file small and explicit; values here are intended for fast iteration
// without searching multiple modules.
namespace DX11RuntimeConfig
{
	// -------------------------------------------------------------------------
	// View distance / fog / sky
	// -------------------------------------------------------------------------
	// Hard clamp for gameplay-visible far clip used by CPythonBackground.
	inline constexpr float kViewDistanceFarClipMax = 50000.0f;
	inline constexpr float kViewDistanceFarClipMin = 1000.0f;
	// Fog tuning for long-distance clarity (higher values = less white wash-out).
	inline constexpr float kViewDistanceFogStartRatio = 0.80f;
	inline constexpr float kViewDistanceFogEndRatio = 0.98f;
	// Skybox scale relative to far clip.
	inline constexpr float kViewDistanceSkyScaleRatio = 0.60f;
	// Sky transition duration used for runtime preset/map updates.
	inline constexpr int kEnvironmentTransitionDurationMs = 300;

	// Sky/cloud blend tuning (DX11 cloud combine shader).
	inline constexpr float kSkyCloudTextureBlendWeight = 0.85f;
	inline constexpr float kSkyCloudTextureMinContribution = 0.30f;
	inline constexpr float kSkyCloudAlphaMin = 0.20f;
	inline constexpr float kSkyCloudVertexAlphaFloor = 0.35f;

	// Debug forcing knobs for sky mode selection.
	inline constexpr bool kSkyDebugForceDiffuseGradient = false;
	inline constexpr bool kSkyDebugForceCloudTexture = false;
	// Dynamic shadow cascade distance multiplier (1.0 = default range).
	// Increase to extend visible shadow range, decrease to tighten for performance.
	inline constexpr float kShadowCascadeDistanceScale = 8000.0f;
	// Default text-tail range used when optimization profile is not overriding it.
	inline constexpr float kTextTailDefaultMaxDistance = 8000.0f;
	// Default camera zoom limits (can still be overridden at runtime by gameplay/python).
	inline constexpr float kCameraMinDistanceDefault = 200.0f;
	inline constexpr float kCameraMaxDistanceDefault = 2500.0f;
	// -------------------------------------------------------------------------
	// Water render (DX11)
	// -------------------------------------------------------------------------
	// Small world-space Z lift to avoid coplanar z-fighting against terrain.
	inline constexpr float kWaterSurfaceZLift = 0.5f;  // Small lift like DX9 (was 50.0f - TOO HIGH!)
	// Minimum alpha enforced in water pixel shader for deterministic visibility.
	inline constexpr float kWaterMinAlpha = 0.40f;
	// Debug mode: render water as solid tint color (ignores animated texture sampling).
	inline constexpr bool kWaterDebugSolidColor = false;  // Normal mode; enable only for diagnostics
	// Clip-space depth bias used in water VS to keep water above coplanar terrain.
	inline constexpr float kWaterDepthBiasClip = 0.0015f;

	// -------------------------------------------------------------------------
	// Granny LOD (actors/buildings/objects)
	// -------------------------------------------------------------------------
	// LOD distance derives from far clip: distance = clamp(far_clip * scale, min, max).
	inline constexpr float kGrannyLodActorScale = 1.00f;
	inline constexpr float kGrannyLodActorMin = 5000.0f;
	inline constexpr float kGrannyLodActorMax = 50000.0f;
	inline constexpr float kGrannyLodBuildingScale = 1.00f;
	inline constexpr float kGrannyLodBuildingMin = 25000.0f;
	inline constexpr float kGrannyLodBuildingMax = 80000.0f;
	// Safety ratio to keep actor distance below building distance.
	inline constexpr float kGrannyLodActorVsBuildingMaxRatio = 1.00f;

	// -------------------------------------------------------------------------
	// Object culling rescue (DX11)
	// -------------------------------------------------------------------------
	// Y-flip rescue retries frustum test with inverted Y to absorb matrix-sign mismatches.
	inline constexpr bool kObjectCullingYFlipRescue = true;
	// Distance rescue keeps building-like objects visible within this range even if frustum says OUTSIDE.
	inline constexpr bool kObjectCullingDistanceRescue = true;
	inline constexpr float kObjectCullingDistanceRescueRange = 55000.0f;

	// -------------------------------------------------------------------------
	// Environment fog (MapManager / world visibility quality)
	// -------------------------------------------------------------------------
	// NOTE: CPythonSystem fog levels are: 0=Light, 1=Middle, 2=Dense.
	// Linear fog distance multipliers per level.
	inline constexpr float kFogLinearScaleLight = 1.25f;
	inline constexpr float kFogLinearScaleMiddle = 1.00f;
	inline constexpr float kFogLinearScaleDense = 0.75f;
	// Density fog base values per level (lower value => farther visibility).
	inline constexpr float kFogDensityLight = 0.000002f;
	inline constexpr float kFogDensityMiddle = 0.000004f;
	inline constexpr float kFogDensityDense = 0.000006f;
	// Safety clamps to keep fog range sane when content has bad values.
	inline constexpr float kFogNearMinDistance = 1000.0f;
	inline constexpr float kFogFarMinDistance = 2000.0f;
	inline constexpr float kFogFarMaxDistance = 50000.0f;
	// Approximation factor used for converting density fog to "far distance".
	inline constexpr float kFogDensityFarApproxFactor = 2.3f;

	// SpeedTree:
	// true  -> force geometry LOD for visible trees (no billboard path)
	// false -> allow normal billboard LOD transitions
	inline constexpr bool kSpeedTreeForceGeometryLOD = false;

	// SpeedTree:
	// true  -> skip billboard render pass in DX11 (textures from far billboard atlas are never sampled)
	// false -> render billboards when LOD requests it
	inline constexpr bool kSpeedTreeDisableBillboardPass = false;

	// Force DropToBillboard behavior in SpeedTree wrapper.
	inline constexpr bool kSpeedTreeDropToBillboard = true;

	// Optional fixed LOD lock for SpeedTree runtime (normalized 0..1).
	inline constexpr bool kSpeedTreeForceFixedLodLevel = false;
	inline constexpr float kSpeedTreeForcedLodLevel = 0.35f;

	// Base LOD factors from tree height used during SetLodLimits().
	inline constexpr float kSpeedTreeNearLodFactor = 2.5f;
	inline constexpr float kSpeedTreeFarLodFactor = 12.0f;

	// Extra long-zoom scaling for LOD limits.
	inline constexpr bool kSpeedTreeUseCameraScaledLodLimits = true;
	inline constexpr float kSpeedTreeCameraDistanceThreshold = 5000.0f;
	inline constexpr float kSpeedTreeFarLodFromCameraScale = 1.10f;
	inline constexpr float kSpeedTreeNearFromFarRatio = 0.20f;
	inline constexpr float kSpeedTreeFarLodMax = 80000.0f;

	// -------------------------------------------------------------------------
	// Grass LOD (SpeedTree grass rendering)
	// -------------------------------------------------------------------------
	// Near distance: Full quality grass (LOD 0 - 100% blades) up to this distance.
	inline constexpr float kGrassLodNearDistance = 500.0f;
	// Far distance: Cull grass beyond this distance completely.
	inline constexpr float kGrassLodFarDistance = 1500.0f;
	// Grass blade size multiplier (affects quad expansion in vertex shader).
	inline constexpr float kGrassSize = 50.0f;
	// Enable camera-scaled grass LOD (similar to tree LOD scaling).
	inline constexpr bool kGrassUseCameraScaledLod = false;
	// Camera distance threshold for grass LOD scaling (when enabled).
	inline constexpr float kGrassCameraDistanceThreshold = 5000.0f;

	// -------------------------------------------------------------------------
	// Grass Texture and Shadows
	// -------------------------------------------------------------------------
	// Enable grass texture sampling (false = use vertex colors only).
	inline constexpr bool kGrassEnableTexture = true;
	// Grass texture file path (absolute path following Metin2 conventions).
	inline constexpr const char* kGrassTexturePath = "d:/ymir work/terrain/grass.dds";
	// Alpha test threshold for grass texture (pixels below this are discarded).
	inline constexpr float kGrassAlphaTestRef = 0.3f;
	// Enable grass shadow rendering (requires kGrassEnableTexture = true).
	inline constexpr bool kGrassEnableShadows = true;
	// Temporary safety switch for DX11 migration:
	// NOT_FULLY_IMPLEMENTED: re-enable after SpeedGrass terrain/material mapping is validated.
	inline constexpr bool kGrassTemporarilyDisableRendering = true;

	
	// -------------------------------------------------------------------------
	// Lens flare (DX11)
	// -------------------------------------------------------------------------
	// Temporal smoothing for occlusion visibility [0..1], higher = faster reaction.
	inline constexpr float kLensFlareOcclusionSmoothing = 0.20f;
	// Probe size in pixels for visibility sampling.
	inline constexpr float kLensFlareOcclusionProbeSizePx = 8.0f;

// -------------------------------------------------------------------------
	// Terrain fog partition distances (legacy terrain helpers)
	// -------------------------------------------------------------------------
	inline constexpr float kTerrainNoFogDistanceRatio = 0.50f;
	inline constexpr float kTerrainFogDistanceRatio = 0.75f;

	// Terrain:
	// true  -> render all loaded terrain patches (coverage-first, higher GPU cost)
	// false -> regular frustum/quadtree cull
	inline constexpr bool kForceFullTerrainCoverage = true;

	// -------------------------------------------------------------------------
	// Environment / Sky / Atmosphere Presets (M3-SKY-BLEND-FIX-74)
	// -------------------------------------------------------------------------
	// Optimal sky scale for visibility (user-preferred default)
	// Lower values = smaller/more distant sky sphere
	// Higher values = larger/more immersive sky sphere
	inline constexpr float kEnvironmentSkyScaleDefault = 29000.0f;

	// Fog distances (Day preset defaults)
	inline constexpr float kEnvironmentFogNearDistanceDay = 5000.0f;
	inline constexpr float kEnvironmentFogFarDistanceDay = 15000.0f;

	// Fog distances (Night preset defaults)
	inline constexpr float kEnvironmentFogNearDistanceNight = 3000.0f;
	inline constexpr float kEnvironmentFogFarDistanceNight = 12000.0f;

	// Lens flare brightness (Day = sun, Night = moon)
	inline constexpr float kEnvironmentLensFlareBrightnessSun = 1.0f;
	inline constexpr float kEnvironmentLensFlareBrightnessMoon = 0.4f;

	// Main flare size (sun/moon)
	inline constexpr float kEnvironmentMainFlareSizeSun = 0.5f;
	inline constexpr float kEnvironmentMainFlareSizeMoon = 0.3f;

	// Screen filter (night blue tint)
	// Color: RGB(0.0-1.0), Alpha: Intensity (0.0-1.0)
	// Recommended night tint: D3DXCOLOR(0.05f, 0.05f, 0.15f, 0.3f) - subtle blue
	inline constexpr float kScreenFilterNightColorR = 0.05f;
	inline constexpr float kScreenFilterNightColorG = 0.05f;
	inline constexpr float kScreenFilterNightColorB = 0.15f;
	inline constexpr float kScreenFilterNightAlpha = 0.3f;

	// Wind strength (ambient atmosphere movement)
	inline constexpr float kEnvironmentWindStrengthDay = 0.3f;
	inline constexpr float kEnvironmentWindStrengthNight = 0.5f;
	inline constexpr float kEnvironmentWindRandomness = 0.3f;

	// Cloud parameters
	inline constexpr float kEnvironmentCloudHeightDefault = 2000.0f;
	inline constexpr float kEnvironmentCloudScaleX = 200.0f;
	inline constexpr float kEnvironmentCloudScaleY = 200.0f;
	inline constexpr float kEnvironmentCloudTextureScale = 2.0f;
	inline constexpr float kEnvironmentCloudSpeedDay = 0.005f;
	inline constexpr float kEnvironmentCloudSpeedNight = 0.002f;

	// Directional lighting (background/terrain)
	// Day preset ambient/diffuse
	inline constexpr float kEnvironmentDirLightDayAmbientR = 0.36f;
	inline constexpr float kEnvironmentDirLightDayAmbientG = 0.38f;
	inline constexpr float kEnvironmentDirLightDayAmbientB = 0.42f;
	inline constexpr float kEnvironmentDirLightDayDiffuseR = 0.92f;
	inline constexpr float kEnvironmentDirLightDayDiffuseG = 0.90f;
	inline constexpr float kEnvironmentDirLightDayDiffuseB = 0.84f;

	// Night preset ambient/diffuse (darker, blue-ish)
	inline constexpr float kEnvironmentDirLightNightAmbientR = 0.08f;
	inline constexpr float kEnvironmentDirLightNightAmbientG = 0.10f;
	inline constexpr float kEnvironmentDirLightNightAmbientB = 0.18f;
	inline constexpr float kEnvironmentDirLightNightDiffuseR = 0.18f;
	inline constexpr float kEnvironmentDirLightNightDiffuseG = 0.22f;
	inline constexpr float kEnvironmentDirLightNightDiffuseB = 0.32f;
}








