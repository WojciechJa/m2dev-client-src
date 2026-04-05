// SPDX-License-Identifier: MIT
// ImGuiGraphicsMetrics.cpp - DX11 Advanced Graphics Metrics Implementation
// ImGui Developer Monitoring Tool - DX11 native only

#include "ImGuiGraphicsMetrics.h"
#include <iostream>

// Singleton instance
CImGuiGraphicsMetrics* CImGuiGraphicsMetrics::ms_pInstance = nullptr;

CImGuiGraphicsMetrics::CImGuiGraphicsMetrics()
    : m_pDevice(nullptr)
    , m_pContext(nullptr)
    , m_pPipelineStatsQuery(nullptr)
    , m_pTimestampStartQuery(nullptr)
    , m_pTimestampEndQuery(nullptr)
    , m_pTimestampDisjointQuery(nullptr)
    , m_bPipelineStatsSupported(false)
    , m_bGPUTimingSupported(false)
    , m_bInitialized(false)
    , m_bPipelineStatsQueryOpen(false)
    , m_bPipelineStatsResultPending(false)
    , m_bTimingQueryOpen(false)
    , m_bTimingResultPending(false)
{
    std::cout << "[GraphicsMetrics] Created" << std::endl;
}

CImGuiGraphicsMetrics::~CImGuiGraphicsMetrics()
{
    Shutdown();
    std::cout << "[GraphicsMetrics] Destroyed" << std::endl;
}

CImGuiGraphicsMetrics* CImGuiGraphicsMetrics::Instance()
{
    return ms_pInstance;
}

bool CImGuiGraphicsMetrics::Create()
{
    if (!ms_pInstance)
    {
        ms_pInstance = new CImGuiGraphicsMetrics();
    }
    return (ms_pInstance != nullptr);
}

void CImGuiGraphicsMetrics::Destroy()
{
    if (ms_pInstance)
    {
        delete ms_pInstance;
        ms_pInstance = nullptr;
    }
}

bool CImGuiGraphicsMetrics::Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
    if (!pDevice || !pContext)
    {
        std::cerr << "[GraphicsMetrics] Invalid parameters" << std::endl;
        return false;
    }

    if (m_bInitialized)
    {
        std::cerr << "[GraphicsMetrics] Already initialized" << std::endl;
        return true;
    }

    m_pDevice = pDevice;
    m_pContext = pContext;

    // Create pipeline statistics query
    if (CreatePipelineStatisticsQuery())
    {
        m_bPipelineStatsSupported = true;
        std::cout << "[GraphicsMetrics] Pipeline statistics query created" << std::endl;
    }
    else
    {
        std::cout << "[GraphicsMetrics] Pipeline statistics not supported" << std::endl;
    }

    // Create timestamp queries
    if (CreateTimestampQueries())
    {
        m_bGPUTimingSupported = true;
        std::cout << "[GraphicsMetrics] Timestamp queries created" << std::endl;
    }
    else
    {
        std::cout << "[GraphicsMetrics] GPU timing not supported" << std::endl;
    }

    m_bInitialized = true;
    return true;
}

void CImGuiGraphicsMetrics::Shutdown()
{
    DestroyQueries();

    m_pDevice = nullptr;
    m_pContext = nullptr;
    m_bInitialized = false;
    m_bPipelineStatsQueryOpen = false;
    m_bPipelineStatsResultPending = false;
    m_bTimingQueryOpen = false;
    m_bTimingResultPending = false;
}

bool CImGuiGraphicsMetrics::CreatePipelineStatisticsQuery()
{
    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
    queryDesc.MiscFlags = 0;

    HRESULT hr = m_pDevice->CreateQuery(&queryDesc, &m_pPipelineStatsQuery);
    if (SUCCEEDED(hr) && m_pPipelineStatsQuery)
    {
        return true;
    }

    return false;
}

bool CImGuiGraphicsMetrics::CreateTimestampQueries()
{
    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_TIMESTAMP;
    queryDesc.MiscFlags = 0;

    HRESULT hrStart = m_pDevice->CreateQuery(&queryDesc, &m_pTimestampStartQuery);
    HRESULT hrEnd = m_pDevice->CreateQuery(&queryDesc, &m_pTimestampEndQuery);

    queryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    HRESULT hrDisjoint = m_pDevice->CreateQuery(&queryDesc, &m_pTimestampDisjointQuery);

    return (SUCCEEDED(hrStart) && m_pTimestampStartQuery &&
            SUCCEEDED(hrEnd) && m_pTimestampEndQuery &&
            SUCCEEDED(hrDisjoint) && m_pTimestampDisjointQuery);
}

void CImGuiGraphicsMetrics::DestroyQueries()
{
    if (m_pPipelineStatsQuery)
    {
        m_pPipelineStatsQuery->Release();
        m_pPipelineStatsQuery = nullptr;
    }

    if (m_pTimestampStartQuery)
    {
        m_pTimestampStartQuery->Release();
        m_pTimestampStartQuery = nullptr;
    }

    if (m_pTimestampEndQuery)
    {
        m_pTimestampEndQuery->Release();
        m_pTimestampEndQuery = nullptr;
    }

    if (m_pTimestampDisjointQuery)
    {
        m_pTimestampDisjointQuery->Release();
        m_pTimestampDisjointQuery = nullptr;
    }

    m_bPipelineStatsQueryOpen = false;
    m_bPipelineStatsResultPending = false;
    m_bTimingQueryOpen = false;
    m_bTimingResultPending = false;
}

void CImGuiGraphicsMetrics::BeginFrame()
{
    if (!m_bInitialized)
        return;

    // Consume previous pipeline statistics result (non-blocking), then begin a new frame query.
    if (m_bPipelineStatsSupported && m_pPipelineStatsQuery)
    {
        if (m_bPipelineStatsResultPending)
        {
            D3D11_QUERY_DATA_PIPELINE_STATISTICS pipelineData = {};
            if (m_pContext->GetData(
                    m_pPipelineStatsQuery,
                    &pipelineData,
                    sizeof(pipelineData),
                    D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
            {
                m_currentPipelineStats.uiVSInvocations = pipelineData.VSInvocations;
                m_currentPipelineStats.uiGSInvocations = pipelineData.GSInvocations;
                m_currentPipelineStats.uiGSPrimitives = pipelineData.GSPrimitives;
                m_currentPipelineStats.uiClipInvocations = pipelineData.CInvocations;
                m_currentPipelineStats.uiClipPrimitives = pipelineData.CPrimitives;
                m_currentPipelineStats.uiPSInvocations = pipelineData.PSInvocations;
                m_currentPipelineStats.uiHSInvocations = pipelineData.HSInvocations;
                m_currentPipelineStats.uiDSInvocations = pipelineData.DSInvocations;
                m_currentPipelineStats.uiCSInvocations = pipelineData.CSInvocations;
                m_bPipelineStatsResultPending = false;
            }
        }

        if (!m_bPipelineStatsResultPending && !m_bPipelineStatsQueryOpen)
        {
            m_pContext->Begin(m_pPipelineStatsQuery);
            m_bPipelineStatsQueryOpen = true;
        }
    }

    // Consume previous timing result (non-blocking), then begin a new timing sample.
    if (m_bGPUTimingSupported && m_pTimestampDisjointQuery && m_pTimestampStartQuery && m_pTimestampEndQuery)
    {
        if (m_bTimingResultPending)
        {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData = {};
            UINT64 timestampStart = 0;
            UINT64 timestampEnd = 0;

            const BOOL hasDisjointData = (m_pContext->GetData(
                m_pTimestampDisjointQuery,
                &disjointData,
                sizeof(disjointData),
                D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK);
            const BOOL hasStartData = (m_pContext->GetData(
                m_pTimestampStartQuery,
                &timestampStart,
                sizeof(timestampStart),
                D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK);
            const BOOL hasEndData = (m_pContext->GetData(
                m_pTimestampEndQuery,
                &timestampEnd,
                sizeof(timestampEnd),
                D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK);

            if (hasDisjointData && hasStartData && hasEndData)
            {
                if (!disjointData.Disjoint && disjointData.Frequency > 0)
                {
                    const float timestampDelta = static_cast<float>(timestampEnd - timestampStart);
                    const float timestampFrequency = static_cast<float>(disjointData.Frequency);
                    m_currentGPUTimingStats.fGPUFrameTimeMs = (timestampDelta / timestampFrequency) * 1000.0f;
                    m_currentGPUTimingStats.bValid = true;
                }
                else
                {
                    m_currentGPUTimingStats.bValid = false;
                }
                m_bTimingResultPending = false;
            }
        }

        if (!m_bTimingResultPending && !m_bTimingQueryOpen)
        {
            m_pContext->Begin(m_pTimestampDisjointQuery);
            m_pContext->End(m_pTimestampStartQuery);
            m_bTimingQueryOpen = true;
        }
    }
}

void CImGuiGraphicsMetrics::EndFrame()
{
    if (!m_bInitialized)
        return;

    // End only if corresponding Begin() was issued this frame.
    if (m_bPipelineStatsSupported && m_pPipelineStatsQuery && m_bPipelineStatsQueryOpen)
    {
        m_pContext->End(m_pPipelineStatsQuery);
        m_bPipelineStatsQueryOpen = false;
        m_bPipelineStatsResultPending = true;
    }

    if (m_bGPUTimingSupported && m_pTimestampDisjointQuery && m_pTimestampEndQuery && m_bTimingQueryOpen)
    {
        m_pContext->End(m_pTimestampEndQuery);
        m_pContext->End(m_pTimestampDisjointQuery);
        m_bTimingQueryOpen = false;
        m_bTimingResultPending = true;
    }
}

void CImGuiGraphicsMetrics::UpdatePipelineStats(const SPipelineStats& stats)
{
    // Manual update (for testing or when queries are not available)
    m_currentPipelineStats = stats;
}

void CImGuiGraphicsMetrics::UpdateResourceStats(const SResourceStats& stats)
{
    m_currentResourceStats = stats;
}

void CImGuiGraphicsMetrics::UpdateSubsystemStats(const SSubsystemStats& stats)
{
    m_currentSubsystemStats = stats;
}

void CImGuiGraphicsMetrics::UpdateGPUTiming(const SGPUTimingStats& stats)
{
    m_currentGPUTimingStats = stats;
}

// Convenience methods for reporting draw calls from different sources
void CImGuiGraphicsMetrics::ReportCharacterDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiCharacterDrawCalls += draws;
    m_currentSubsystemStats.uiCharacterPrims += prims;
    m_currentSubsystemStats.fCharacterCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportMapTreeDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiMapTreeDrawCalls += draws;
    m_currentSubsystemStats.uiMapTreePrims += prims;
    m_currentSubsystemStats.fMapTreeCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportMapAreaDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiMapAreaDrawCalls += draws;
    m_currentSubsystemStats.uiMapAreaPrims += prims;
    m_currentSubsystemStats.fMapAreaCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportTerrainDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiTerrainDrawCalls += draws;
    m_currentSubsystemStats.uiTerrainPrims += prims;
    m_currentSubsystemStats.fTerrainCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportUITreeDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiUITreeDrawCalls += draws;
    m_currentSubsystemStats.uiUITreePrims += prims;
    m_currentSubsystemStats.fUITreeCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportUIMGUIDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiUIMGUIDrawCalls += draws;
    m_currentSubsystemStats.uiUIMGUIPrims += prims;
    m_currentSubsystemStats.fUIMGUICPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportEffectsDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiEffectsDrawCalls += draws;
    m_currentSubsystemStats.uiEffectsPrims += prims;
    m_currentSubsystemStats.fEffectsCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportBlockerDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiBlockerDrawCalls += draws;
    m_currentSubsystemStats.uiBlockerPrims += prims;
    m_currentSubsystemStats.fBlockerCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportBgEffectDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiBgEffectDrawCalls += draws;
    m_currentSubsystemStats.uiBgEffectPrims += prims;
    m_currentSubsystemStats.fBgEffectCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportShadowDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiShadowDrawCalls += draws;
    m_currentSubsystemStats.uiShadowPrims += prims;
    m_currentSubsystemStats.fShadowCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportSkyDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiSkyDrawCalls += draws;
    m_currentSubsystemStats.uiSkyPrims += prims;
    m_currentSubsystemStats.fSkyCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportCloudDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiCloudDrawCalls += draws;
    m_currentSubsystemStats.uiCloudPrims += prims;
    m_currentSubsystemStats.fCloudCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ReportWaterDrawCalls(UINT32 draws, UINT64 prims, float cpuMs)
{
    m_currentSubsystemStats.uiWaterDrawCalls += draws;
    m_currentSubsystemStats.uiWaterPrims += prims;
    m_currentSubsystemStats.fWaterCPUMs += cpuMs;
}

void CImGuiGraphicsMetrics::ResetSubsystemStats()
{
    m_currentSubsystemStats.uiCharacterDrawCalls = 0;
    m_currentSubsystemStats.uiMapTreeDrawCalls = 0;
    m_currentSubsystemStats.uiMapAreaDrawCalls = 0;
    m_currentSubsystemStats.uiTerrainDrawCalls = 0;
    m_currentSubsystemStats.uiUITreeDrawCalls = 0;
    m_currentSubsystemStats.uiUIMGUIDrawCalls = 0;
    m_currentSubsystemStats.uiEffectsDrawCalls = 0;
    m_currentSubsystemStats.uiBlockerDrawCalls = 0;
    m_currentSubsystemStats.uiBgEffectDrawCalls = 0;
    m_currentSubsystemStats.uiShadowDrawCalls = 0;
    m_currentSubsystemStats.uiSkyDrawCalls = 0;
    m_currentSubsystemStats.uiCloudDrawCalls = 0;
    m_currentSubsystemStats.uiWaterDrawCalls = 0;

    m_currentSubsystemStats.uiCharacterPrims = 0;
    m_currentSubsystemStats.uiMapTreePrims = 0;
    m_currentSubsystemStats.uiMapAreaPrims = 0;
    m_currentSubsystemStats.uiTerrainPrims = 0;
    m_currentSubsystemStats.uiUITreePrims = 0;
    m_currentSubsystemStats.uiUIMGUIPrims = 0;
    m_currentSubsystemStats.uiEffectsPrims = 0;
    m_currentSubsystemStats.uiBlockerPrims = 0;
    m_currentSubsystemStats.uiBgEffectPrims = 0;
    m_currentSubsystemStats.uiShadowPrims = 0;
    m_currentSubsystemStats.uiSkyPrims = 0;
    m_currentSubsystemStats.uiCloudPrims = 0;
    m_currentSubsystemStats.uiWaterPrims = 0;

    m_currentSubsystemStats.fCharacterCPUMs = 0.0f;
    m_currentSubsystemStats.fMapTreeCPUMs = 0.0f;
    m_currentSubsystemStats.fMapAreaCPUMs = 0.0f;
    m_currentSubsystemStats.fTerrainCPUMs = 0.0f;
    m_currentSubsystemStats.fUITreeCPUMs = 0.0f;
    m_currentSubsystemStats.fUIMGUICPUMs = 0.0f;
    m_currentSubsystemStats.fEffectsCPUMs = 0.0f;
    m_currentSubsystemStats.fBlockerCPUMs = 0.0f;
    m_currentSubsystemStats.fBgEffectCPUMs = 0.0f;
    m_currentSubsystemStats.fShadowCPUMs = 0.0f;
    m_currentSubsystemStats.fSkyCPUMs = 0.0f;
    m_currentSubsystemStats.fCloudCPUMs = 0.0f;
    m_currentSubsystemStats.fWaterCPUMs = 0.0f;
}

// Global helper function for extern declaration
void ResetImGuiSubsystemStats()
{
    if (CImGuiGraphicsMetrics::Instance())
    {
        CImGuiGraphicsMetrics::Instance()->ResetSubsystemStats();
    }
}

// Global helper function for reporting character draw calls
void ReportImGuiCharacterDrawCalls(UINT32 draws, UINT64 prims)
{
    if (CImGuiGraphicsMetrics::Instance())
    {
        CImGuiGraphicsMetrics::Instance()->ReportCharacterDrawCalls(draws, prims);
    }
}

// Global helper function for reporting Map/Tree draw calls
void ReportImGuiMapTreeDrawCalls(UINT32 draws, UINT64 prims)
{
    if (CImGuiGraphicsMetrics::Instance())
    {
        CImGuiGraphicsMetrics::Instance()->ReportMapTreeDrawCalls(draws, prims);
    }
}

// Global helper function for reporting Map/Terrain draw calls
void ReportImGuiTerrainDrawCalls(UINT32 draws, UINT64 prims)
{
    if (CImGuiGraphicsMetrics::Instance())
    {
        CImGuiGraphicsMetrics::Instance()->ReportTerrainDrawCalls(draws, prims);
    }
}

// Global helper function for reporting Water draw calls
void ReportImGuiWaterDrawCalls(UINT32 draws, UINT64 prims)
{
    if (CImGuiGraphicsMetrics::Instance())
    {
        CImGuiGraphicsMetrics::Instance()->ReportWaterDrawCalls(draws, prims);
    }
}

// Global helper function for reporting Shadow draw calls
void ReportImGuiShadowDrawCalls(UINT32 draws, UINT64 prims)
{
    if (CImGuiGraphicsMetrics::Instance())
    {
        CImGuiGraphicsMetrics::Instance()->ReportShadowDrawCalls(draws, prims);
    }
}

// Global helper function for reporting Effects draw calls
void ReportImGuiEffectsDrawCalls(UINT32 draws, UINT64 prims)
{
    if (CImGuiGraphicsMetrics::Instance())
    {
        CImGuiGraphicsMetrics::Instance()->ReportEffectsDrawCalls(draws, prims);
    }
}

// M3-SPEEDTREE-ATLAS-09: Global helper for async texture stats
void ReportImGuiAsyncTextureStats(
    UINT32 pending,
    UINT32 completed,
    UINT32 failed,
    UINT32 cacheSize,
    UINT32 cacheHits,
    UINT32 cacheMisses,
    UINT32 budgetMB,
    UINT32 usageMB)
{
    if (CImGuiGraphicsMetrics::Instance())
    {
        SResourceStats stats = CImGuiGraphicsMetrics::Instance()->GetResourceStats();
        stats.uiAsyncTexturePending = pending;
        stats.uiAsyncTextureCompleted = completed;
        stats.uiAsyncTextureFailed = failed;
        stats.uiTextureCacheSize = cacheSize;
        stats.uiTextureCacheHits = cacheHits;
        stats.uiTextureCacheMisses = cacheMisses;
        stats.uiTextureMemoryBudgetMB = budgetMB;
        stats.uiTextureMemoryUsageMB = usageMB;
        CImGuiGraphicsMetrics::Instance()->UpdateResourceStats(stats);
    }
}
