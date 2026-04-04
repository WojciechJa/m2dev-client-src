// SPDX-License-Identifier: MIT
// ImGuiGraphicsMetrics.h - DX11 Advanced Graphics Metrics Collection
// ImGui Developer Monitoring Tool - DX11 native only
//
// Purpose: Collect DX11 pipeline statistics, texture/buffer usage, subsystem metrics
// Integration: GrpDeviceDX11 → CImGuiGraphicsMetrics → ImGuiManager overlay

#pragma once

#include <d3d11.h>
#include <cstdint>

// DX11 Pipeline Statistics (from D3D11_QUERY_PIPELINE_STATISTICS)
struct SPipelineStats
{
    UINT64 uiVSInvocations;      // Vertex shader invocations
    UINT64 uiGSInvocations;      // Geometry shader invocations
    UINT64 uiGSPrimitives;       // Geometry shader primitives output
    UINT64 uiClipInvocations;    // Clipping invocations
    UINT64 uiClipPrimitives;     // Clipping primitives output
    UINT64 uiPSInvocations;      // Pixel shader invocations
    UINT64 uiHSInvocations;      // Hull shader invocations
    UINT64 uiDSInvocations;      // Domain shader invocations;
    UINT64 uiCSInvocations;      // Compute shader invocations

    SPipelineStats()
        : uiVSInvocations(0)
        , uiGSInvocations(0)
        , uiGSPrimitives(0)
        , uiClipInvocations(0)
        , uiClipPrimitives(0)
        , uiPSInvocations(0)
        , uiHSInvocations(0)
        , uiDSInvocations(0)
        , uiCSInvocations(0)
    {}
};

// DX11 Resource Usage Statistics
struct SResourceStats
{
    // Texture statistics
    UINT32 uiTextureCount;       // Number of textures currently bound
    UINT64 uiTextureMemoryMB;    // Total texture memory in MB

    // Buffer statistics
    UINT32 uiVertexBuffers;      // Bound vertex buffers
    UINT32 uiIndexBuffers;       // Bound index buffers
    UINT32 uiConstantBuffers;    // Bound constant buffers
    UINT32 uiStreamOutBuffers;   // Bound streamout buffers

    SResourceStats()
        : uiTextureCount(0)
        , uiTextureMemoryMB(0)
        , uiVertexBuffers(0)
        , uiIndexBuffers(0)
        , uiConstantBuffers(0)
        , uiStreamOutBuffers(0)
    {}
};

// Per-subsystem draw call statistics (extended for detailed breakdown)
struct SSubsystemStats
{
    // Original categories
    UINT32 uiTerrainDrawCalls;    // Terrain rendering
    UINT32 uiWaterDrawCalls;      // Water rendering
    UINT32 uiSpeedTreeDrawCalls;  // SpeedTree rendering
    UINT32 uiObjectDrawCalls;     // Static/dynamic objects
    UINT32 uiUIDrawCalls;         // UI rendering
    UINT32 uiOtherDrawCalls;      // Other/uncategorized

    // New detailed source categories (from imgui_newstructure.md)
    UINT32 uiCharacterDrawCalls;  // Character models
    UINT32 uiMapTreeDrawCalls;    // Map/Tree rendering
    UINT32 uiMapAreaDrawCalls;    // Map/Area rendering
    UINT32 uiUITreeDrawCalls;     // UI-Tree rendering
    UINT32 uiUIMGUIDrawCalls;     // UI-IMGUI overlay
    UINT32 uiEffectsDrawCalls;    // Effects rendering
    UINT32 uiBlockerDrawCalls;    // Blocker rendering
    UINT32 uiBgEffectDrawCalls;   // Background effects
    UINT32 uiShadowDrawCalls;     // Shadow rendering
    UINT32 uiSkyDrawCalls;        // Sky rendering
    UINT32 uiCloudDrawCalls;      // Cloud rendering

    // Primitive counts per source
    UINT64 uiCharacterPrims;
    UINT64 uiMapTreePrims;
    UINT64 uiMapAreaPrims;
    UINT64 uiTerrainPrims;
    UINT64 uiUITreePrims;
    UINT64 uiUIMGUIPrims;
    UINT64 uiEffectsPrims;
    UINT64 uiBlockerPrims;
    UINT64 uiBgEffectPrims;
    UINT64 uiShadowPrims;
    UINT64 uiSkyPrims;
    UINT64 uiCloudPrims;
    UINT64 uiWaterPrims;

    // CPU time per source (milliseconds)
    float fCharacterCPUMs;
    float fMapTreeCPUMs;
    float fMapAreaCPUMs;
    float fTerrainCPUMs;
    float fUITreeCPUMs;
    float fUIMGUICPUMs;
    float fEffectsCPUMs;
    float fBlockerCPUMs;
    float fBgEffectCPUMs;
    float fShadowCPUMs;
    float fSkyCPUMs;
    float fCloudCPUMs;
    float fWaterCPUMs;

    SSubsystemStats()
        : uiTerrainDrawCalls(0)
        , uiWaterDrawCalls(0)
        , uiSpeedTreeDrawCalls(0)
        , uiObjectDrawCalls(0)
        , uiUIDrawCalls(0)
        , uiOtherDrawCalls(0)
        , uiCharacterDrawCalls(0)
        , uiMapTreeDrawCalls(0)
        , uiMapAreaDrawCalls(0)
        , uiUITreeDrawCalls(0)
        , uiUIMGUIDrawCalls(0)
        , uiEffectsDrawCalls(0)
        , uiBlockerDrawCalls(0)
        , uiBgEffectDrawCalls(0)
        , uiShadowDrawCalls(0)
        , uiSkyDrawCalls(0)
        , uiCloudDrawCalls(0)
        , uiCharacterPrims(0)
        , uiMapTreePrims(0)
        , uiMapAreaPrims(0)
        , uiTerrainPrims(0)
        , uiUITreePrims(0)
        , uiUIMGUIPrims(0)
        , uiEffectsPrims(0)
        , uiBlockerPrims(0)
        , uiBgEffectPrims(0)
        , uiShadowPrims(0)
        , uiSkyPrims(0)
        , uiCloudPrims(0)
        , uiWaterPrims(0)
        , fCharacterCPUMs(0.0f)
        , fMapTreeCPUMs(0.0f)
        , fMapAreaCPUMs(0.0f)
        , fTerrainCPUMs(0.0f)
        , fUITreeCPUMs(0.0f)
        , fUIMGUICPUMs(0.0f)
        , fEffectsCPUMs(0.0f)
        , fBlockerCPUMs(0.0f)
        , fBgEffectCPUMs(0.0f)
        , fShadowCPUMs(0.0f)
        , fSkyCPUMs(0.0f)
        , fCloudCPUMs(0.0f)
        , fWaterCPUMs(0.0f)
    {}

    UINT32 GetTotalDrawCalls() const
    {
        return uiTerrainDrawCalls + uiWaterDrawCalls + uiSpeedTreeDrawCalls +
               uiObjectDrawCalls + uiUIDrawCalls + uiOtherDrawCalls +
               uiCharacterDrawCalls + uiMapTreeDrawCalls + uiMapAreaDrawCalls +
               uiUITreeDrawCalls + uiUIMGUIDrawCalls + uiEffectsDrawCalls +
               uiBlockerDrawCalls + uiBgEffectDrawCalls + uiShadowDrawCalls +
               uiSkyDrawCalls + uiCloudDrawCalls;
    }

    UINT64 GetTotalPrimitives() const
    {
        return uiCharacterPrims + uiMapTreePrims + uiMapAreaPrims +
               uiTerrainPrims + uiUITreePrims + uiUIMGUIPrims +
               uiEffectsPrims + uiBlockerPrims + uiBgEffectPrims +
               uiShadowPrims + uiSkyPrims + uiCloudPrims + uiWaterPrims;
    }
};

// GPU Timing Statistics
struct SGPUTimingStats
{
    float fGPUFrameTimeMs;       // GPU frame time in milliseconds
    float fGPUTimeSinceLastMs;   // Time since last frame (ms)
    bool bValid;                 // Whether timing data is valid

    SGPUTimingStats()
        : fGPUFrameTimeMs(0.0f)
        , fGPUTimeSinceLastMs(0.0f)
        , bValid(false)
    {}
};

class CImGuiGraphicsMetrics
{
public:
    // Singleton interface
    static CImGuiGraphicsMetrics* Instance();
    static bool Create();
    static void Destroy();

    // Initialize queries (called from GrpDeviceDX11::Create)
    bool Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
    void Shutdown();

    // Frame management
    void BeginFrame();
    void EndFrame();

    // Update statistics (called from rendering code)
    void UpdatePipelineStats(const SPipelineStats& stats);
    void UpdateResourceStats(const SResourceStats& stats);
    void UpdateSubsystemStats(const SSubsystemStats& stats);
    void UpdateGPUTiming(const SGPUTimingStats& stats);

    // Get current statistics
    const SPipelineStats& GetPipelineStats() const { return m_currentPipelineStats; }
    const SResourceStats& GetResourceStats() const { return m_currentResourceStats; }
    const SSubsystemStats& GetSubsystemStats() const { return m_currentSubsystemStats; }
    const SGPUTimingStats& GetGPUTimingStats() const { return m_currentGPUTimingStats; }

    // Convenience methods for reporting draw calls from different sources
    void ReportCharacterDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportMapTreeDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportMapAreaDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportTerrainDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportUITreeDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportUIMGUIDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportEffectsDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportBlockerDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportBgEffectDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportShadowDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportSkyDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportCloudDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);
    void ReportWaterDrawCalls(UINT32 draws, UINT64 prims = 0, float cpuMs = 0.0f);

    // Reset subsystem counters (call at start of each frame)
    void ResetSubsystemStats();

    // Query support detection
    bool IsPipelineStatsSupported() const { return m_bPipelineStatsSupported; }
    bool IsGPUTimingSupported() const { return m_bGPUTimingSupported; }

private:
    CImGuiGraphicsMetrics();
    ~CImGuiGraphicsMetrics();

    // Prevent copying
    CImGuiGraphicsMetrics(const CImGuiGraphicsMetrics&) = delete;
    CImGuiGraphicsMetrics& operator=(const CImGuiGraphicsMetrics&) = delete;

    // Query management
    bool CreatePipelineStatisticsQuery();
    bool CreateTimestampQueries();
    void DestroyQueries();

    // Member variables
    ID3D11Device* m_pDevice;
    ID3D11DeviceContext* m_pContext;

    // DX11 Queries
    ID3D11Query* m_pPipelineStatsQuery;
    ID3D11Query* m_pTimestampStartQuery;
    ID3D11Query* m_pTimestampEndQuery;
    ID3D11Query* m_pTimestampDisjointQuery;

    // Query support flags
    bool m_bPipelineStatsSupported;
    bool m_bGPUTimingSupported;
    bool m_bInitialized;
    bool m_bPipelineStatsQueryOpen;
    bool m_bPipelineStatsResultPending;
    bool m_bTimingQueryOpen;
    bool m_bTimingResultPending;

    // Current frame statistics
    SPipelineStats m_currentPipelineStats;
    SResourceStats m_currentResourceStats;
    SSubsystemStats m_currentSubsystemStats;
    SGPUTimingStats m_currentGPUTimingStats;

    // Singleton instance
    static CImGuiGraphicsMetrics* ms_pInstance;
};

// Global access macro
#define GraphicsMetrics() CImGuiGraphicsMetrics::Instance()

// Global helper function for resetting subsystem stats (called from GrpDeviceDX11)
extern void ResetImGuiSubsystemStats();

// Global helper function for reporting character draw calls (called from ThingInstance/ModelInstanceRender)
extern void ReportImGuiCharacterDrawCalls(UINT32 draws, UINT64 prims = 0);

// Global helper function for reporting Map/Tree draw calls (called from SpeedTreeForestDirectX)
extern void ReportImGuiMapTreeDrawCalls(UINT32 draws, UINT64 prims = 0);

// Global helper function for reporting Map/Terrain draw calls (called from MapOutdoorRenderDX11)
extern void ReportImGuiTerrainDrawCalls(UINT32 draws, UINT64 prims = 0);

// Global helper function for reporting Water draw calls (called from MapOutdoorRenderDX11)
extern void ReportImGuiWaterDrawCalls(UINT32 draws, UINT64 prims = 0);

// Global helper function for reporting Shadow draw calls (called from ModelInstanceRender)
extern void ReportImGuiShadowDrawCalls(UINT32 draws, UINT64 prims = 0);

// Global helper function for reporting Effects draw calls (called from EffectLib DX11 paths)
extern void ReportImGuiEffectsDrawCalls(UINT32 draws, UINT64 prims = 0);
