// SPDX-License-Identifier: MIT
// ImGuiMetricsCollector.h - Real-time Metrics Collection System
// ImGui Developer Monitoring Tool - DX11 native only
//
// Purpose: Collect and manage performance telemetry for ImGui overlay
// Integration: PythonApplication::Loop() → CImGuiMetricsCollector::Update()

#pragma once

#include <d3d11.h>
#include <vector>
#include <deque>
#include <cstdint>

// Single frame snapshot of all metrics
struct SMetricsSnapshot
{
    // Performance metrics
    float fFrameTime;           // Frame time in milliseconds
    float fFPS;                 // Frames per second
    float fCPUFrameTime;        // CPU frame processing time (ms)

    // DX11 Backend metrics
    uint32_t uDX11FeatureLevel; // D3D_FEATURE_LEVEL_*
    uint32_t uWorldPortMask;    // CGraphicDeviceDX11 world port mask
    uint32_t uMissingPortMask;  // Missing world subsystems
    uint32_t uWorldObservedMask;
    uint32_t uWorldSubmittedMask;
    uint32_t uWorldApplicableMask;
    uint32_t uWorldCommittedMask;
    uint32_t uLightRegisteredStatic;
    uint32_t uLightRegisteredDynamic;
    uint32_t uLightActiveStatic;
    uint32_t uLightActiveDynamic;
    uint32_t uLightRequestedActive;
    uint32_t uLightBoundActive;
    uint32_t uLightClippedBySlot;
    uint32_t uLightSlotCapacity;
    uint32_t uLightSkipIndex;
    uint32_t uTerrainPatches;
    uint32_t uTerrainSplats;
    uint32_t uWaterPatches;
    uint32_t uObjectSubmitted;
    uint32_t uEffectSubmitted;
    uint32_t uEffectParticleSubmitted;
    uint32_t uEffectMeshSubmitted;
    uint32_t uSpeedTreeSubmitted;
    uint32_t uNoRTVWithPSCount;
    uint32_t uNoRTVWithPSIndexedCount;
    uint32_t uNoRTVWithPSNonIndexedCount;
    uint32_t uNoRTVWithPSLastTopology;
    uint32_t uNoRTVWithPSLastElements;
    uint32_t uNoRTVWithPSLastDepthBound;
    uint32_t uUnsupportedRenderStateCount;
    uint32_t uUnsupportedRenderStateLastType;
    uint32_t uUnsupportedRenderStateLastValue;
    uint32_t uFogEnable;
    uint32_t uFogMode;
    uint32_t uFogRangeEnable;
    uint32_t uFogColor;
    uint32_t uFogDensity;
    uint32_t uFogStart;
    uint32_t uFogEnd;
    uint32_t uVSConstClampCount;
    uint32_t uVSConstClampLastRegister;
    uint32_t uVSConstClampLastRequested;
    uint32_t uVSConstClampLastApplied;
    uint32_t uVSConstSetCallCount;
    uint32_t uVSConstSetRegisterCount;
    uint32_t uVSConstSetLastRegister;
    uint32_t uVSConstSetLastCount;
    uint32_t uVSConstUploadCount;
    uint32_t uVSConstUploadBytes;
    uint32_t uVSConstUploadStartRegister;
    uint32_t uVSConstUploadEndRegister;
    uint32_t uPSConstClampCount;
    uint32_t uPSConstClampLastRegister;
    uint32_t uPSConstClampLastRequested;
    uint32_t uPSConstClampLastApplied;
    uint32_t uPSConstSetCallCount;
    uint32_t uPSConstSetRegisterCount;
    uint32_t uPSConstSetLastRegister;
    uint32_t uPSConstSetLastCount;
    uint32_t uPSConstUploadCount;
    uint32_t uPSConstUploadBytes;
    uint32_t uPSConstUploadStartRegister;
    uint32_t uPSConstUploadEndRegister;
    uint32_t uWorldSubmitMismatchCount;
    uint32_t uWorldSubmitMismatchActive;
    uint32_t uWorldSubmitMismatchTelemetryObserved;
    uint32_t uWorldSubmitMismatchTelemetrySubmitted;
    uint32_t uWorldSubmitMismatchTelemetryApplicable;
    uint32_t uWorldSubmitMismatchTelemetryCommitted;
    uint32_t uWorldSubmitMismatchGateObserved;
    uint32_t uWorldSubmitMismatchGateSubmitted;
    uint32_t uWorldSubmitMismatchGateApplicable;
    uint32_t uWorldSubmitMismatchGateCommitted;

    // Rendering metrics
    uint32_t uDrawCalls;        // DX11 draw calls per frame
    uint32_t uPrimitiveCount;   // Primitives rendered
    uint32_t uVertexCount;      // Vertices processed
    uint32_t uEntityCount;      // Active game entities (character manager)
    uint32_t uWorldPassCount;   // Active world subsystem passes this frame

    // Memory metrics (MB)
    float fWorkingSetMB;        // Process working set
    float fGPUMemoryMB;         // GPU memory usage (if available)

    // System metrics
    uint64_t qwTimestamp;       // QueryPerformanceCounter value

    // Default constructor
    SMetricsSnapshot()
        : fFrameTime(0.0f)
        , fFPS(0.0f)
        , fCPUFrameTime(0.0f)
        , uDX11FeatureLevel(0)
        , uWorldPortMask(0)
        , uMissingPortMask(0)
        , uWorldObservedMask(0)
        , uWorldSubmittedMask(0)
        , uWorldApplicableMask(0)
        , uWorldCommittedMask(0)
        , uLightRegisteredStatic(0)
        , uLightRegisteredDynamic(0)
        , uLightActiveStatic(0)
        , uLightActiveDynamic(0)
        , uLightRequestedActive(0)
        , uLightBoundActive(0)
        , uLightClippedBySlot(0)
        , uLightSlotCapacity(0)
        , uLightSkipIndex(0)
        , uTerrainPatches(0)
        , uTerrainSplats(0)
        , uWaterPatches(0)
        , uObjectSubmitted(0)
        , uEffectSubmitted(0)
        , uEffectParticleSubmitted(0)
        , uEffectMeshSubmitted(0)
        , uSpeedTreeSubmitted(0)
        , uNoRTVWithPSCount(0)
        , uNoRTVWithPSIndexedCount(0)
        , uNoRTVWithPSNonIndexedCount(0)
        , uNoRTVWithPSLastTopology(0)
        , uNoRTVWithPSLastElements(0)
        , uNoRTVWithPSLastDepthBound(0)
        , uUnsupportedRenderStateCount(0)
        , uUnsupportedRenderStateLastType(0)
        , uUnsupportedRenderStateLastValue(0)
        , uFogEnable(0)
        , uFogMode(0)
        , uFogRangeEnable(0)
        , uFogColor(0)
        , uFogDensity(0)
        , uFogStart(0)
        , uFogEnd(0)
        , uVSConstClampCount(0)
        , uVSConstClampLastRegister(0)
        , uVSConstClampLastRequested(0)
        , uVSConstClampLastApplied(0)
        , uVSConstSetCallCount(0)
        , uVSConstSetRegisterCount(0)
        , uVSConstSetLastRegister(0)
        , uVSConstSetLastCount(0)
        , uVSConstUploadCount(0)
        , uVSConstUploadBytes(0)
        , uVSConstUploadStartRegister(0)
        , uVSConstUploadEndRegister(0)
        , uPSConstClampCount(0)
        , uPSConstClampLastRegister(0)
        , uPSConstClampLastRequested(0)
        , uPSConstClampLastApplied(0)
        , uPSConstSetCallCount(0)
        , uPSConstSetRegisterCount(0)
        , uPSConstSetLastRegister(0)
        , uPSConstSetLastCount(0)
        , uPSConstUploadCount(0)
        , uPSConstUploadBytes(0)
        , uPSConstUploadStartRegister(0)
        , uPSConstUploadEndRegister(0)
        , uWorldSubmitMismatchCount(0)
        , uWorldSubmitMismatchActive(0)
        , uWorldSubmitMismatchTelemetryObserved(0)
        , uWorldSubmitMismatchTelemetrySubmitted(0)
        , uWorldSubmitMismatchTelemetryApplicable(0)
        , uWorldSubmitMismatchTelemetryCommitted(0)
        , uWorldSubmitMismatchGateObserved(0)
        , uWorldSubmitMismatchGateSubmitted(0)
        , uWorldSubmitMismatchGateApplicable(0)
        , uWorldSubmitMismatchGateCommitted(0)
        , uDrawCalls(0)
        , uPrimitiveCount(0)
        , uVertexCount(0)
        , uEntityCount(0)
        , uWorldPassCount(0)
        , fWorkingSetMB(0.0f)
        , fGPUMemoryMB(0.0f)
        , qwTimestamp(0)
    {}
};

// Metrics history entry with timestamp
struct SMetricsHistoryEntry
{
    SMetricsSnapshot snapshot;
    uint64_t qwTimestampUTC;    // System time (UTC)

    SMetricsHistoryEntry()
        : qwTimestampUTC(0)
    {}
};

class CImGuiMetricsCollector
{
public:
    // Singleton interface
    static CImGuiMetricsCollector* Instance();
    static bool Create();
    static void Destroy();

    // Update metrics for current frame (called from PythonApplication::Loop)
    void Update(
        float fFrameTime,
        float fFPS,
        uint32_t uDrawCalls = 0,
        uint32_t uPrimitiveCount = 0,
        uint32_t uVertexCount = 0,
        uint32_t uEntityCount = 0,
        uint32_t uWorldPassCount = 0
    );

    // Set DX11-specific metrics (called from GrpDeviceDX11)
    void SetDX11Metrics(
        uint32_t uFeatureLevel,
        uint32_t uWorldPortMask,
        uint32_t uMissingPortMask,
        uint32_t uWorldObservedMask = 0,
        uint32_t uWorldSubmittedMask = 0,
        uint32_t uWorldApplicableMask = 0,
        uint32_t uWorldCommittedMask = 0,
        uint32_t uLightRegisteredStatic = 0,
        uint32_t uLightRegisteredDynamic = 0,
        uint32_t uLightActiveStatic = 0,
        uint32_t uLightActiveDynamic = 0,
        uint32_t uLightRequestedActive = 0,
        uint32_t uLightBoundActive = 0,
        uint32_t uLightClippedBySlot = 0,
        uint32_t uLightSlotCapacity = 0,
        uint32_t uLightSkipIndex = 0,
        uint32_t uTerrainPatches = 0,
        uint32_t uTerrainSplats = 0,
        uint32_t uWaterPatches = 0,
        uint32_t uObjectSubmitted = 0,
        uint32_t uEffectSubmitted = 0,
        uint32_t uEffectParticleSubmitted = 0,
        uint32_t uEffectMeshSubmitted = 0,
        uint32_t uSpeedTreeSubmitted = 0,
        uint32_t uNoRTVWithPSCount = 0,
        uint32_t uNoRTVWithPSIndexedCount = 0,
        uint32_t uNoRTVWithPSNonIndexedCount = 0,
        uint32_t uNoRTVWithPSLastTopology = 0,
        uint32_t uNoRTVWithPSLastElements = 0,
        uint32_t uNoRTVWithPSLastDepthBound = 0,
        uint32_t uUnsupportedRenderStateCount = 0,
        uint32_t uUnsupportedRenderStateLastType = 0,
        uint32_t uUnsupportedRenderStateLastValue = 0,
        uint32_t uFogEnable = 0,
        uint32_t uFogMode = 0,
        uint32_t uFogRangeEnable = 0,
        uint32_t uFogColor = 0,
        uint32_t uFogDensity = 0,
        uint32_t uFogStart = 0,
        uint32_t uFogEnd = 0,
        uint32_t uVSConstClampCount = 0,
        uint32_t uVSConstClampLastRegister = 0,
        uint32_t uVSConstClampLastRequested = 0,
        uint32_t uVSConstClampLastApplied = 0,
        uint32_t uVSConstSetCallCount = 0,
        uint32_t uVSConstSetRegisterCount = 0,
        uint32_t uVSConstSetLastRegister = 0,
        uint32_t uVSConstSetLastCount = 0,
        uint32_t uVSConstUploadCount = 0,
        uint32_t uVSConstUploadBytes = 0,
        uint32_t uVSConstUploadStartRegister = 0,
        uint32_t uVSConstUploadEndRegister = 0,
        uint32_t uPSConstClampCount = 0,
        uint32_t uPSConstClampLastRegister = 0,
        uint32_t uPSConstClampLastRequested = 0,
        uint32_t uPSConstClampLastApplied = 0,
        uint32_t uPSConstSetCallCount = 0,
        uint32_t uPSConstSetRegisterCount = 0,
        uint32_t uPSConstSetLastRegister = 0,
        uint32_t uPSConstSetLastCount = 0,
        uint32_t uPSConstUploadCount = 0,
        uint32_t uPSConstUploadBytes = 0,
        uint32_t uPSConstUploadStartRegister = 0,
        uint32_t uPSConstUploadEndRegister = 0,
        uint32_t uWorldSubmitMismatchCount = 0,
        uint32_t uWorldSubmitMismatchActive = 0,
        uint32_t uWorldSubmitMismatchTelemetryObserved = 0,
        uint32_t uWorldSubmitMismatchTelemetrySubmitted = 0,
        uint32_t uWorldSubmitMismatchTelemetryApplicable = 0,
        uint32_t uWorldSubmitMismatchTelemetryCommitted = 0,
        uint32_t uWorldSubmitMismatchGateObserved = 0,
        uint32_t uWorldSubmitMismatchGateSubmitted = 0,
        uint32_t uWorldSubmitMismatchGateApplicable = 0,
        uint32_t uWorldSubmitMismatchGateCommitted = 0
    );

    // Get current snapshot
    const SMetricsSnapshot& GetCurrentSnapshot() const { return m_currentSnapshot; }

    // Get history buffers (for graphing in Phase 4)
    const std::deque<SMetricsHistoryEntry>& GetHistory() const { return m_history; }

    // History management
    size_t GetMaxHistorySize() const { return m_maxHistorySize; }
    void SetMaxHistorySize(size_t maxSize) { m_maxHistorySize = maxSize; }

    // Computed metrics
    float GetAverageFPS() const;
    float GetMinFPS() const;
    float GetMaxFPS() const;
    float GetAverageFrameTime() const;

    // Memory queries
    void UpdateMemoryMetrics();

private:
    CImGuiMetricsCollector();
    ~CImGuiMetricsCollector();

    // Prevent copying
    CImGuiMetricsCollector(const CImGuiMetricsCollector&) = delete;
    CImGuiMetricsCollector& operator=(const CImGuiMetricsCollector&) = delete;

    // History management
    void AddToHistory(const SMetricsSnapshot& snapshot);
    void TrimHistory();

    // Member variables
    SMetricsSnapshot m_currentSnapshot;
    std::deque<SMetricsHistoryEntry> m_history;
    size_t m_maxHistorySize;

    // Running statistics
    float m_fTotalFPS;
    float m_fTotalFrameTime;
    uint32_t m_uFrameCount;

    // Singleton instance
    static CImGuiMetricsCollector* ms_pInstance;
};

// Global access macro
#define MetricsCollector() CImGuiMetricsCollector::Instance()
