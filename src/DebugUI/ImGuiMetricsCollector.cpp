// SPDX-License-Identifier: MIT
// ImGuiMetricsCollector.cpp - Real-time Metrics Collection System Implementation
// ImGui Developer Monitoring Tool - DX11 native only

#include "ImGuiMetricsCollector.h"
#include <psapi.h>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "psapi.lib")

// Singleton instance
CImGuiMetricsCollector* CImGuiMetricsCollector::ms_pInstance = nullptr;

CImGuiMetricsCollector::CImGuiMetricsCollector()
    : m_maxHistorySize(300)  // 300 seconds @ 1 sample/sec = 5 minutes
    , m_fTotalFPS(0.0f)
    , m_fTotalFrameTime(0.0f)
    , m_uFrameCount(0)
{
    // History container is std::deque; it grows in chunks and has no reserve().
    std::cout << "[MetricsCollector] Initialized with " << m_maxHistorySize << " sample history" << std::endl;
}

CImGuiMetricsCollector::~CImGuiMetricsCollector()
{
    m_history.clear();
    std::cout << "[MetricsCollector] Shutdown complete" << std::endl;
}

CImGuiMetricsCollector* CImGuiMetricsCollector::Instance()
{
    return ms_pInstance;
}

bool CImGuiMetricsCollector::Create()
{
    if (!ms_pInstance)
    {
        ms_pInstance = new CImGuiMetricsCollector();
    }
    return (ms_pInstance != nullptr);
}

void CImGuiMetricsCollector::Destroy()
{
    if (ms_pInstance)
    {
        delete ms_pInstance;
        ms_pInstance = nullptr;
    }
}

void CImGuiMetricsCollector::Update(
    float fFrameTime,
    float fFPS,
    uint32_t uDrawCalls,
    uint32_t uPrimitiveCount,
    uint32_t uVertexCount,
    uint32_t uEntityCount,
    uint32_t uWorldPassCount)
{
    // Update current snapshot
    m_currentSnapshot.fFrameTime = fFrameTime;
    m_currentSnapshot.fFPS = fFPS;
    m_currentSnapshot.uDrawCalls = uDrawCalls;
    m_currentSnapshot.uPrimitiveCount = uPrimitiveCount;
    m_currentSnapshot.uVertexCount = uVertexCount;
    m_currentSnapshot.uEntityCount = uEntityCount;
    m_currentSnapshot.uWorldPassCount = uWorldPassCount;

    // Update timestamp
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&m_currentSnapshot.qwTimestamp));

    // Update memory metrics (once per frame)
    UpdateMemoryMetrics();

    // Add to history
    AddToHistory(m_currentSnapshot);

    // Update running statistics
    m_fTotalFPS += fFPS;
    m_fTotalFrameTime += fFrameTime;
    m_uFrameCount++;
}

void CImGuiMetricsCollector::SetDX11Metrics(
    uint32_t uFeatureLevel,
    uint32_t uWorldPortMask,
    uint32_t uMissingPortMask,
    uint32_t uWorldObservedMask,
    uint32_t uWorldSubmittedMask,
    uint32_t uWorldApplicableMask,
    uint32_t uWorldCommittedMask,
    uint32_t uLightRegisteredStatic,
    uint32_t uLightRegisteredDynamic,
    uint32_t uLightActiveStatic,
    uint32_t uLightActiveDynamic,
    uint32_t uLightRequestedActive,
    uint32_t uLightBoundActive,
    uint32_t uLightClippedBySlot,
    uint32_t uLightSlotCapacity,
    uint32_t uLightSkipIndex,
    uint32_t uTerrainPatches,
    uint32_t uTerrainSplats,
    uint32_t uWaterPatches,
    uint32_t uObjectSubmitted,
    uint32_t uEffectSubmitted,
    uint32_t uEffectParticleSubmitted,
    uint32_t uEffectMeshSubmitted,
    uint32_t uSpeedTreeSubmitted,
    uint32_t uNoRTVWithPSCount,
    uint32_t uNoRTVWithPSIndexedCount,
    uint32_t uNoRTVWithPSNonIndexedCount,
    uint32_t uNoRTVWithPSLastTopology,
    uint32_t uNoRTVWithPSLastElements,
    uint32_t uNoRTVWithPSLastDepthBound,
    uint32_t uUnsupportedRenderStateCount,
    uint32_t uUnsupportedRenderStateLastType,
    uint32_t uUnsupportedRenderStateLastValue,
    uint32_t uFogEnable,
    uint32_t uFogMode,
    uint32_t uFogRangeEnable,
    uint32_t uFogColor,
    uint32_t uFogDensity,
    uint32_t uFogStart,
    uint32_t uFogEnd,
    uint32_t uVSConstClampCount,
    uint32_t uVSConstClampLastRegister,
    uint32_t uVSConstClampLastRequested,
    uint32_t uVSConstClampLastApplied,
    uint32_t uVSConstSetCallCount,
    uint32_t uVSConstSetRegisterCount,
    uint32_t uVSConstSetLastRegister,
    uint32_t uVSConstSetLastCount,
    uint32_t uVSConstUploadCount,
    uint32_t uVSConstUploadBytes,
    uint32_t uVSConstUploadStartRegister,
    uint32_t uVSConstUploadEndRegister,
    uint32_t uPSConstClampCount,
    uint32_t uPSConstClampLastRegister,
    uint32_t uPSConstClampLastRequested,
    uint32_t uPSConstClampLastApplied,
    uint32_t uPSConstSetCallCount,
    uint32_t uPSConstSetRegisterCount,
    uint32_t uPSConstSetLastRegister,
    uint32_t uPSConstSetLastCount,
    uint32_t uPSConstUploadCount,
    uint32_t uPSConstUploadBytes,
    uint32_t uPSConstUploadStartRegister,
    uint32_t uPSConstUploadEndRegister,
    uint32_t uWorldSubmitMismatchCount,
    uint32_t uWorldSubmitMismatchActive,
    uint32_t uWorldSubmitMismatchTelemetryObserved,
    uint32_t uWorldSubmitMismatchTelemetrySubmitted,
    uint32_t uWorldSubmitMismatchTelemetryApplicable,
    uint32_t uWorldSubmitMismatchTelemetryCommitted,
    uint32_t uWorldSubmitMismatchGateObserved,
    uint32_t uWorldSubmitMismatchGateSubmitted,
    uint32_t uWorldSubmitMismatchGateApplicable,
    uint32_t uWorldSubmitMismatchGateCommitted)
{
    m_currentSnapshot.uDX11FeatureLevel = uFeatureLevel;
    m_currentSnapshot.uWorldPortMask = uWorldPortMask;
    m_currentSnapshot.uMissingPortMask = uMissingPortMask;
    m_currentSnapshot.uWorldObservedMask = uWorldObservedMask;
    m_currentSnapshot.uWorldSubmittedMask = uWorldSubmittedMask;
    m_currentSnapshot.uWorldApplicableMask = uWorldApplicableMask;
    m_currentSnapshot.uWorldCommittedMask = uWorldCommittedMask;
    m_currentSnapshot.uLightRegisteredStatic = uLightRegisteredStatic;
    m_currentSnapshot.uLightRegisteredDynamic = uLightRegisteredDynamic;
    m_currentSnapshot.uLightActiveStatic = uLightActiveStatic;
    m_currentSnapshot.uLightActiveDynamic = uLightActiveDynamic;
    m_currentSnapshot.uLightRequestedActive = uLightRequestedActive;
    m_currentSnapshot.uLightBoundActive = uLightBoundActive;
    m_currentSnapshot.uLightClippedBySlot = uLightClippedBySlot;
    m_currentSnapshot.uLightSlotCapacity = uLightSlotCapacity;
    m_currentSnapshot.uLightSkipIndex = uLightSkipIndex;
    m_currentSnapshot.uTerrainPatches = uTerrainPatches;
    m_currentSnapshot.uTerrainSplats = uTerrainSplats;
    m_currentSnapshot.uWaterPatches = uWaterPatches;
    m_currentSnapshot.uObjectSubmitted = uObjectSubmitted;
    m_currentSnapshot.uEffectSubmitted = uEffectSubmitted;
    m_currentSnapshot.uEffectParticleSubmitted = uEffectParticleSubmitted;
    m_currentSnapshot.uEffectMeshSubmitted = uEffectMeshSubmitted;
    m_currentSnapshot.uSpeedTreeSubmitted = uSpeedTreeSubmitted;
    m_currentSnapshot.uNoRTVWithPSCount = uNoRTVWithPSCount;
    m_currentSnapshot.uNoRTVWithPSIndexedCount = uNoRTVWithPSIndexedCount;
    m_currentSnapshot.uNoRTVWithPSNonIndexedCount = uNoRTVWithPSNonIndexedCount;
    m_currentSnapshot.uNoRTVWithPSLastTopology = uNoRTVWithPSLastTopology;
    m_currentSnapshot.uNoRTVWithPSLastElements = uNoRTVWithPSLastElements;
    m_currentSnapshot.uNoRTVWithPSLastDepthBound = uNoRTVWithPSLastDepthBound;
    m_currentSnapshot.uUnsupportedRenderStateCount = uUnsupportedRenderStateCount;
    m_currentSnapshot.uUnsupportedRenderStateLastType = uUnsupportedRenderStateLastType;
    m_currentSnapshot.uUnsupportedRenderStateLastValue = uUnsupportedRenderStateLastValue;
    m_currentSnapshot.uFogEnable = uFogEnable;
    m_currentSnapshot.uFogMode = uFogMode;
    m_currentSnapshot.uFogRangeEnable = uFogRangeEnable;
    m_currentSnapshot.uFogColor = uFogColor;
    m_currentSnapshot.uFogDensity = uFogDensity;
    m_currentSnapshot.uFogStart = uFogStart;
    m_currentSnapshot.uFogEnd = uFogEnd;
    m_currentSnapshot.uVSConstClampCount = uVSConstClampCount;
    m_currentSnapshot.uVSConstClampLastRegister = uVSConstClampLastRegister;
    m_currentSnapshot.uVSConstClampLastRequested = uVSConstClampLastRequested;
    m_currentSnapshot.uVSConstClampLastApplied = uVSConstClampLastApplied;
    m_currentSnapshot.uVSConstSetCallCount = uVSConstSetCallCount;
    m_currentSnapshot.uVSConstSetRegisterCount = uVSConstSetRegisterCount;
    m_currentSnapshot.uVSConstSetLastRegister = uVSConstSetLastRegister;
    m_currentSnapshot.uVSConstSetLastCount = uVSConstSetLastCount;
    m_currentSnapshot.uVSConstUploadCount = uVSConstUploadCount;
    m_currentSnapshot.uVSConstUploadBytes = uVSConstUploadBytes;
    m_currentSnapshot.uVSConstUploadStartRegister = uVSConstUploadStartRegister;
    m_currentSnapshot.uVSConstUploadEndRegister = uVSConstUploadEndRegister;
    m_currentSnapshot.uPSConstClampCount = uPSConstClampCount;
    m_currentSnapshot.uPSConstClampLastRegister = uPSConstClampLastRegister;
    m_currentSnapshot.uPSConstClampLastRequested = uPSConstClampLastRequested;
    m_currentSnapshot.uPSConstClampLastApplied = uPSConstClampLastApplied;
    m_currentSnapshot.uPSConstSetCallCount = uPSConstSetCallCount;
    m_currentSnapshot.uPSConstSetRegisterCount = uPSConstSetRegisterCount;
    m_currentSnapshot.uPSConstSetLastRegister = uPSConstSetLastRegister;
    m_currentSnapshot.uPSConstSetLastCount = uPSConstSetLastCount;
    m_currentSnapshot.uPSConstUploadCount = uPSConstUploadCount;
    m_currentSnapshot.uPSConstUploadBytes = uPSConstUploadBytes;
    m_currentSnapshot.uPSConstUploadStartRegister = uPSConstUploadStartRegister;
    m_currentSnapshot.uPSConstUploadEndRegister = uPSConstUploadEndRegister;
    m_currentSnapshot.uWorldSubmitMismatchCount = uWorldSubmitMismatchCount;
    m_currentSnapshot.uWorldSubmitMismatchActive = uWorldSubmitMismatchActive;
    m_currentSnapshot.uWorldSubmitMismatchTelemetryObserved = uWorldSubmitMismatchTelemetryObserved;
    m_currentSnapshot.uWorldSubmitMismatchTelemetrySubmitted = uWorldSubmitMismatchTelemetrySubmitted;
    m_currentSnapshot.uWorldSubmitMismatchTelemetryApplicable = uWorldSubmitMismatchTelemetryApplicable;
    m_currentSnapshot.uWorldSubmitMismatchTelemetryCommitted = uWorldSubmitMismatchTelemetryCommitted;
    m_currentSnapshot.uWorldSubmitMismatchGateObserved = uWorldSubmitMismatchGateObserved;
    m_currentSnapshot.uWorldSubmitMismatchGateSubmitted = uWorldSubmitMismatchGateSubmitted;
    m_currentSnapshot.uWorldSubmitMismatchGateApplicable = uWorldSubmitMismatchGateApplicable;
    m_currentSnapshot.uWorldSubmitMismatchGateCommitted = uWorldSubmitMismatchGateCommitted;
}

void CImGuiMetricsCollector::UpdateMemoryMetrics()
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
        sizeof(pmc)))
    {
        m_currentSnapshot.fWorkingSetMB = static_cast<float>(pmc.WorkingSetSize) / (1024.0f * 1024.0f);
    }

    // GPU memory is not directly queryable in DX11 without vendor extensions
    // This will be implemented in Phase 3 if needed
    m_currentSnapshot.fGPUMemoryMB = 0.0f;
}

void CImGuiMetricsCollector::AddToHistory(const SMetricsSnapshot& snapshot)
{
    SMetricsHistoryEntry entry;
    entry.snapshot = snapshot;
    entry.qwTimestampUTC = GetTickCount64();  // Simplified timestamp

    m_history.push_back(entry);

    // Trim history if needed
    TrimHistory();
}

void CImGuiMetricsCollector::TrimHistory()
{
    while (m_history.size() > m_maxHistorySize)
    {
        m_history.pop_front();
    }
}

float CImGuiMetricsCollector::GetAverageFPS() const
{
    if (m_uFrameCount == 0)
        return 0.0f;
    return m_fTotalFPS / static_cast<float>(m_uFrameCount);
}

float CImGuiMetricsCollector::GetMinFPS() const
{
    if (m_history.empty())
        return 0.0f;

    float fMinFPS = m_history[0].snapshot.fFPS;
    for (const auto& entry : m_history)
    {
        if (entry.snapshot.fFPS < fMinFPS && entry.snapshot.fFPS > 0.0f)
            fMinFPS = entry.snapshot.fFPS;
    }
    return fMinFPS;
}

float CImGuiMetricsCollector::GetMaxFPS() const
{
    if (m_history.empty())
        return 0.0f;

    float fMaxFPS = m_history[0].snapshot.fFPS;
    for (const auto& entry : m_history)
    {
        if (entry.snapshot.fFPS > fMaxFPS)
            fMaxFPS = entry.snapshot.fFPS;
    }
    return fMaxFPS;
}

float CImGuiMetricsCollector::GetAverageFrameTime() const
{
    if (m_uFrameCount == 0)
        return 0.0f;
    return m_fTotalFrameTime / static_cast<float>(m_uFrameCount);
}
