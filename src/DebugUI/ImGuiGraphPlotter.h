// SPDX-License-Identifier: MIT
// ImGuiGraphPlotter.h - Graph Visualization System
// ImGui Developer Monitoring Tool - DX11 native only
//
// Purpose: Render real-time graphs for metrics visualization
// Integration: ImGuiManager overlay → CImGuiGraphPlotter → ImGui::PlotLines/PlotHistogram

#pragma once

#include <imgui.h>
#include <vector>
#include <deque>
#include <cstdint>

// Graph configuration
struct SGraphConfig
{
    ImVec4 vColorLine;           // Line color
    ImVec4 vColorFill;           // Fill color (under line)
    ImVec4 vColorBackground;     // Background color
    float fMinValue;             // Minimum Y value (auto if 0.0f)
    float fMaxValue;             // Maximum Y value (auto if 0.0f)
    float fLineWidth;            // Line width
    bool bShowFill;              // Show filled area under line
    bool bShowGrid;              // Show grid lines
    bool bShowLegend;            // Show legend
    bool bAutoScale;             // Auto-scale Y axis

    SGraphConfig()
        : vColorLine(ImVec4(0.0f, 0.75f, 1.0f, 1.0f))
        , vColorFill(ImVec4(0.0f, 0.75f, 1.0f, 0.3f))
        , vColorBackground(ImVec4(0.1f, 0.1f, 0.1f, 0.5f))
        , fMinValue(0.0f)
        , fMaxValue(0.0f)
        , fLineWidth(2.0f)
        , bShowFill(true)
        , bShowGrid(true)
        , bShowLegend(true)
        , bAutoScale(true)
    {}
};

// Histogram bucket configuration
struct SHistogramConfig
{
    std::vector<float> vfBucketBoundaries;  // Bucket boundaries
    std::vector<ImVec4> vvBucketColors;     // Colors per bucket
    std::vector<const char*> vszBucketLabels; // Bucket labels
    float fMinValue;                         // Minimum value
    float fMaxValue;                         // Maximum value (auto if 0.0f)
    bool bShowLabels;                        // Show bucket labels
    bool bShowPercentages;                   // Show percentage in tooltip

    SHistogramConfig()
        : fMinValue(0.0f)
        , fMaxValue(0.0f)
        , bShowLabels(true)
        , bShowPercentages(true)
    {
        // Default FPS buckets (0-30, 30-60, 60-120, 120+)
        vfBucketBoundaries = {30.0f, 60.0f, 120.0f};
        vvBucketColors = {
            ImVec4(1.0f, 0.0f, 0.0f, 0.7f),  // Red (bad)
            ImVec4(1.0f, 0.5f, 0.0f, 0.7f),  // Orange (ok)
            ImVec4(1.0f, 1.0f, 0.0f, 0.7f),  // Yellow (good)
            ImVec4(0.0f, 1.0f, 0.0f, 0.7f)   // Green (excellent)
        };
        vszBucketLabels = {"< 30", "30-60", "60-120", "> 120"};
    }
};

// Graph control state
struct SGraphControls
{
    bool bPaused;               // Pause graph updates
    float fTimeWindow;          // Time window in seconds (0 = all history)
    float fScrollSpeed;         // Scroll speed multiplier
    int iMaxDataPoints;         // Maximum data points to display
    bool bExportToClipboard;    // Export graph data to clipboard

    SGraphControls()
        : bPaused(false)
        , fTimeWindow(60.0f)    // Default: show last 60 seconds
        , fScrollSpeed(1.0f)
        , iMaxDataPoints(300)   // Default: 300 data points
        , bExportToClipboard(false)
    {}
};

class CImGuiGraphPlotter
{
public:
    // Singleton interface
    static CImGuiGraphPlotter* Instance();
    static bool Create();
    static void Destroy();

    // Line graph rendering
    void PlotLineGraph(
        const char* szLabel,
        const std::deque<float>& vfData,
        const SGraphConfig& config = SGraphConfig(),
        const ImVec2& vSize = ImVec2(0, 100)
    );

    // Multi-line graph (multiple data series)
    void PlotMultiLineGraph(
        const char* szLabel,
        const std::vector<std::deque<float>>& vvfData,
        const std::vector<const char*>& vszLabels,
        const std::vector<ImVec4>& vvColors,
        const ImVec2& vSize = ImVec2(0, 100)
    );

    // Histogram rendering
    void PlotHistogram(
        const char* szLabel,
        const std::deque<float>& vfData,
        const SHistogramConfig& config = SHistogramConfig(),
        const ImVec2& vSize = ImVec2(0, 100)
    );

    // Get graph controls
    SGraphControls& GetControls() { return m_controls; }
    const SGraphControls& GetControls() const { return m_controls; }

    // Render control panel
    void RenderControlPanel();

    // Export functionality
    void ExportToClipboard(const std::deque<float>& vfData, const char* szLabel);

    // Helper functions
    static void CalculateHistogramBuckets(
        const std::deque<float>& vfData,
        const std::vector<float>& vfBoundaries,
        std::vector<int>& viBucketCounts
    );

    static void GetAutoScale(
        const std::deque<float>& vfData,
        float& fMinOut,
        float& fMaxOut
    );

private:
    CImGuiGraphPlotter();
    ~CImGuiGraphPlotter();

    // Prevent copying
    CImGuiGraphPlotter(const CImGuiGraphPlotter&) = delete;
    CImGuiGraphPlotter& operator=(const CImGuiGraphPlotter&) = delete;

    // Internal rendering helpers
    void DrawGrid(const ImVec2& vGraphMin, const ImVec2& vGraphMax, int iGridLines = 5);
    void DrawLegend(const std::vector<const char*>& vszLabels, const std::vector<ImVec4>& vvColors);

    // Member variables
    SGraphControls m_controls;

    // Singleton instance
    static CImGuiGraphPlotter* ms_pInstance;
};

// Global access macro
#define GraphPlotter() CImGuiGraphPlotter::Instance()
