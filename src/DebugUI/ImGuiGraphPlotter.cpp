// SPDX-License-Identifier: MIT
// ImGuiGraphPlotter.cpp - Graph Visualization System Implementation
// ImGui Developer Monitoring Tool - DX11 native only

#include "ImGuiGraphPlotter.h"
#include "ImGuiMetricsCollector.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <windows.h>

// Singleton instance
CImGuiGraphPlotter* CImGuiGraphPlotter::ms_pInstance = nullptr;

CImGuiGraphPlotter::CImGuiGraphPlotter()
{
    std::cout << "[GraphPlotter] Created" << std::endl;
}

CImGuiGraphPlotter::~CImGuiGraphPlotter()
{
    std::cout << "[GraphPlotter] Destroyed" << std::endl;
}

CImGuiGraphPlotter* CImGuiGraphPlotter::Instance()
{
    return ms_pInstance;
}

bool CImGuiGraphPlotter::Create()
{
    if (!ms_pInstance)
    {
        ms_pInstance = new CImGuiGraphPlotter();
    }
    return (ms_pInstance != nullptr);
}

void CImGuiGraphPlotter::Destroy()
{
    if (ms_pInstance)
    {
        delete ms_pInstance;
        ms_pInstance = nullptr;
    }
}

void CImGuiGraphPlotter::PlotLineGraph(
    const char* szLabel,
    const std::deque<float>& vfData,
    const SGraphConfig& config,
    const ImVec2& vSize)
{
    if (vfData.empty())
        return;

    // Apply time window filter
    std::vector<float> vfFilteredData;
    if (m_controls.fTimeWindow > 0.0f && vfData.size() > static_cast<size_t>(m_controls.fTimeWindow))
    {
        const size_t uiStartIndex = vfData.size() - static_cast<size_t>(m_controls.fTimeWindow);
        vfFilteredData.assign(vfData.begin() + uiStartIndex, vfData.end());
    }
    else
    {
        vfFilteredData.assign(vfData.begin(), vfData.end());
    }

    // Apply max data points limit
    if (m_controls.iMaxDataPoints > 0 && vfFilteredData.size() > static_cast<size_t>(m_controls.iMaxDataPoints))
    {
        const size_t uiStartIndex = vfFilteredData.size() - static_cast<size_t>(m_controls.iMaxDataPoints);
        vfFilteredData.assign(vfFilteredData.begin() + uiStartIndex, vfFilteredData.end());
    }

    if (vfFilteredData.empty())
        return;

    // Calculate Y range
    float fMinY = config.fMinValue;
    float fMaxY = config.fMaxValue;

    if (config.bAutoScale)
    {
        GetAutoScale(vfData, fMinY, fMaxY);
    }

    // Child window for scrolling
    ImGui::BeginChild(szLabel, vSize, true, ImGuiWindowFlags_None);

    // Get draw list
    ImDrawList* pDrawList = ImGui::GetWindowDrawList();
    const ImVec2 vCursorScreenPos = ImGui::GetCursorScreenPos();
    const ImVec2 vWindowSize = ImGui::GetContentRegionAvail();

    // Draw background
    if (config.vColorBackground.w > 0.0f)
    {
        pDrawList->AddRectFilled(
            vCursorScreenPos,
            ImVec2(vCursorScreenPos.x + vWindowSize.x, vCursorScreenPos.y + vWindowSize.y),
            ImGui::ColorConvertFloat4ToU32(config.vColorBackground)
        );
    }

    // Draw grid
    if (config.bShowGrid)
    {
        DrawGrid(vCursorScreenPos, ImVec2(vCursorScreenPos.x + vWindowSize.x, vCursorScreenPos.y + vWindowSize.y));
    }

    // Plot line
    const float fXStep = vWindowSize.x / std::max(1.0f, static_cast<float>(vfFilteredData.size() - 1));
    const float fYRange = std::max(0.01f, fMaxY - fMinY);

    std::vector<ImVec2> vvPoints;
    vvPoints.reserve(vfFilteredData.size());

    for (size_t i = 0; i < vfFilteredData.size(); ++i)
    {
        const float fNormalizedY = (vfFilteredData[i] - fMinY) / fYRange;
        const float fY = vCursorScreenPos.y + vWindowSize.y - (fNormalizedY * vWindowSize.y);
        const float fX = vCursorScreenPos.x + (i * fXStep);
        vvPoints.push_back(ImVec2(fX, fY));
    }

    // Draw filled area
    if (config.bShowFill && !vvPoints.empty())
    {
        std::vector<ImVec2> vvFillPoints = vvPoints;
        vvFillPoints.insert(vvFillPoints.begin(), ImVec2(vvPoints[0].x, vCursorScreenPos.y + vWindowSize.y));
        vvFillPoints.push_back(ImVec2(vvPoints.back().x, vCursorScreenPos.y + vWindowSize.y));

        pDrawList->AddConvexPolyFilled(
            vvFillPoints.data(),
            static_cast<int>(vvFillPoints.size()),
            ImGui::ColorConvertFloat4ToU32(config.vColorFill)
        );
    }

    // Draw line
    if (vvPoints.size() > 1)
    {
        pDrawList->AddPolyline(
            vvPoints.data(),
            static_cast<int>(vvPoints.size()),
            ImGui::ColorConvertFloat4ToU32(config.vColorLine),
            false,
            config.fLineWidth
        );
    }

    // Draw label
    if (config.bShowLegend)
    {
        ImGui::SetCursorScreenPos(ImVec2(vCursorScreenPos.x + 5, vCursorScreenPos.y + 5));
        ImGui::TextColored(config.vColorLine, "%s: %.2f", szLabel, vfData.back());
    }

    ImGui::EndChild();
}

void CImGuiGraphPlotter::PlotMultiLineGraph(
    const char* szLabel,
    const std::vector<std::deque<float>>& vvfData,
    const std::vector<const char*>& vszLabels,
    const std::vector<ImVec4>& vvColors,
    const ImVec2& vSize)
{
    if (vvfData.empty() || vvfData.size() != vszLabels.size() || vvfData.size() != vvColors.size())
        return;

    ImGui::BeginChild(szLabel, vSize, true, ImGuiWindowFlags_None);

    ImDrawList* pDrawList = ImGui::GetWindowDrawList();
    const ImVec2 vCursorScreenPos = ImGui::GetCursorScreenPos();
    const ImVec2 vWindowSize = ImGui::GetContentRegionAvail();

    // Find global Y range
    float fGlobalMin = 0.0f;
    float fGlobalMax = 0.0f;
    for (const auto& vfData : vvfData)
    {
        float fMin, fMax;
        GetAutoScale(vfData, fMin, fMax);
        fGlobalMin = std::min(fGlobalMin, fMin);
        fGlobalMax = std::max(fGlobalMax, fMax);
    }

    const float fYRange = std::max(0.01f, fGlobalMax - fGlobalMin);

    // Plot each line
    for (size_t series = 0; series < vvfData.size(); ++series)
    {
        const std::deque<float>& vfData = vvfData[series];
        if (vfData.empty())
            continue;

        std::vector<float> vfFilteredData;
        if (m_controls.fTimeWindow > 0.0f && vfData.size() > static_cast<size_t>(m_controls.fTimeWindow))
        {
            const size_t uiStartIndex = vfData.size() - static_cast<size_t>(m_controls.fTimeWindow);
            vfFilteredData.assign(vfData.begin() + uiStartIndex, vfData.end());
        }
        else
        {
            vfFilteredData.assign(vfData.begin(), vfData.end());
        }

        if (vfFilteredData.empty())
            continue;

        const float fXStep = vWindowSize.x / std::max(1.0f, static_cast<float>(vfFilteredData.size() - 1));

        std::vector<ImVec2> vvPoints;
        vvPoints.reserve(vfFilteredData.size());

        for (size_t i = 0; i < vfFilteredData.size(); ++i)
        {
            const float fNormalizedY = (vfFilteredData[i] - fGlobalMin) / fYRange;
            const float fY = vCursorScreenPos.y + vWindowSize.y - (fNormalizedY * vWindowSize.y);
            const float fX = vCursorScreenPos.x + (i * fXStep);
            vvPoints.push_back(ImVec2(fX, fY));
        }

        if (vvPoints.size() > 1)
        {
            pDrawList->AddPolyline(
                vvPoints.data(),
                static_cast<int>(vvPoints.size()),
                ImGui::ColorConvertFloat4ToU32(vvColors[series]),
                false,
                2.0f
            );
        }

        // Draw legend
        ImGui::SetCursorScreenPos(ImVec2(vCursorScreenPos.x + 5, vCursorScreenPos.y + 5 + (series * 20)));
        ImGui::TextColored(vvColors[series], "%s: %.2f", vszLabels[series], vfData.back());
    }

    ImGui::EndChild();
}

void CImGuiGraphPlotter::PlotHistogram(
    const char* szLabel,
    const std::deque<float>& vfData,
    const SHistogramConfig& config,
    const ImVec2& vSize)
{
    if (vfData.empty())
        return;

    ImGui::BeginChild(szLabel, vSize, true, ImGuiWindowFlags_None);

    // Calculate bucket counts
    std::vector<int> viBucketCounts(config.vfBucketBoundaries.size() + 1, 0);
    CalculateHistogramBuckets(vfData, config.vfBucketBoundaries, viBucketCounts);

    // Calculate total for percentages
    const int iTotal = vfData.size();

    // Draw histogram
    const ImVec2 vCursorScreenPos = ImGui::GetCursorScreenPos();
    const ImVec2 vWindowSize = ImGui::GetContentRegionAvail();
    ImDrawList* pDrawList = ImGui::GetWindowDrawList();

    const float fBarWidth = vWindowSize.x / viBucketCounts.size();
    const float fMaxCount = *std::max_element(viBucketCounts.begin(), viBucketCounts.end());

    for (size_t i = 0; i < viBucketCounts.size(); ++i)
    {
        const float fBarHeight = (fMaxCount > 0) ? (viBucketCounts[i] / static_cast<float>(fMaxCount)) * vWindowSize.y : 0.0f;
        const ImVec2 vBarMin(vCursorScreenPos.x + (i * fBarWidth), vCursorScreenPos.y + vWindowSize.y - fBarHeight);
        const ImVec2 vBarMax(vBarMin.x + fBarWidth - 2, vCursorScreenPos.y + vWindowSize.y);

        pDrawList->AddRectFilled(
            vBarMin,
            vBarMax,
            ImGui::ColorConvertFloat4ToU32(config.vvBucketColors[i % config.vvBucketColors.size()])
        );

        // Show count and percentage on hover
        if (ImGui::IsMouseHoveringRect(vBarMin, vBarMax))
        {
            ImGui::SetTooltip("%s: %d (%.1f%%)",
                config.vszBucketLabels[i % config.vszBucketLabels.size()],
                viBucketCounts[i],
                (iTotal > 0) ? (viBucketCounts[i] * 100.0f / iTotal) : 0.0f
            );
        }
    }

    // Draw labels
    if (config.bShowLabels)
    {
        for (size_t i = 0; i < config.vszBucketLabels.size() && i < viBucketCounts.size(); ++i)
        {
            const float fX = vCursorScreenPos.x + (i * fBarWidth) + (fBarWidth / 2);
            ImGui::SetCursorScreenPos(ImVec2(fX - 20, vCursorScreenPos.y + vWindowSize.y + 5));
            ImGui::Text("%s", config.vszBucketLabels[i]);
        }
    }

    ImGui::EndChild();
}

void CImGuiGraphPlotter::RenderControlPanel()
{
    if (ImGui::CollapsingHeader("Graph Controls"))
    {
        ImGui::Checkbox("Pause Graphs", &m_controls.bPaused);
        ImGui::SliderFloat("Time Window (s)", &m_controls.fTimeWindow, 0.0f, 300.0f, "%.0f");
        ImGui::SliderInt("Max Data Points", &m_controls.iMaxDataPoints, 10, 1000);

        if (ImGui::Button("Reset Controls"))
        {
            m_controls = SGraphControls();
        }

        ImGui::SameLine();
        if (ImGui::Button("Export FPS to Clipboard"))
        {
            if (CImGuiMetricsCollector::Instance())
            {
                const auto& history = CImGuiMetricsCollector::Instance()->GetHistory();
                if (!history.empty())
                {
                    std::vector<float> vfFPS;
                    vfFPS.reserve(history.size());
                    for (const auto& entry : history)
                    {
                        vfFPS.push_back(entry.snapshot.fFPS);
                    }
                    ExportToClipboard(std::deque<float>(vfFPS.begin(), vfFPS.end()), "FPS");
                }
            }
        }
    }
}

void CImGuiGraphPlotter::ExportToClipboard(const std::deque<float>& vfData, const char* szLabel)
{
    std::string sText;
    sText.reserve(vfData.size() * 10);

    sText += szLabel;
    sText += " Data:\n";

    for (size_t i = 0; i < vfData.size(); ++i)
    {
        char szNumber[64];
        _snprintf_s(szNumber, sizeof(szNumber), _TRUNCATE, "%.6f", vfData[i]);
        sText += szNumber;
        sText += "\n";
    }

    if (OpenClipboard(nullptr))
    {
        EmptyClipboard();
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sText.size() + 1);
        if (hGlobal)
        {
            char* pszData = static_cast<char*>(GlobalLock(hGlobal));
            if (pszData)
            {
                memcpy(pszData, sText.c_str(), sText.size() + 1);
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_TEXT, hGlobal);
                std::cout << "[GraphPlotter] Exported " << szLabel << " data to clipboard (" << vfData.size() << " samples)" << std::endl;
            }
        }
        CloseClipboard();
    }
}

void CImGuiGraphPlotter::CalculateHistogramBuckets(
    const std::deque<float>& vfData,
    const std::vector<float>& vfBoundaries,
    std::vector<int>& viBucketCounts)
{
    viBucketCounts.assign(vfBoundaries.size() + 1, 0);

    for (const float fValue : vfData)
    {
        size_t uiBucket = 0;
        for (; uiBucket < vfBoundaries.size(); ++uiBucket)
        {
            if (fValue < vfBoundaries[uiBucket])
                break;
        }
        viBucketCounts[uiBucket]++;
    }
}

void CImGuiGraphPlotter::GetAutoScale(const std::deque<float>& vfData, float& fMinOut, float& fMaxOut)
{
    if (vfData.empty())
    {
        fMinOut = 0.0f;
        fMaxOut = 1.0f;
        return;
    }

    fMinOut = vfData[0];
    fMaxOut = vfData[0];

    for (const float fValue : vfData)
    {
        fMinOut = std::min(fMinOut, fValue);
        fMaxOut = std::max(fMaxOut, fValue);
    }

    // Add 10% padding
    const float fRange = fMaxOut - fMinOut;
    if (fRange > 0.0f)
    {
        fMaxOut += fRange * 0.1f;
        fMinOut = std::max(0.0f, fMinOut - fRange * 0.1f);
    }
}

void CImGuiGraphPlotter::DrawGrid(const ImVec2& vGraphMin, const ImVec2& vGraphMax, int iGridLines)
{
    ImDrawList* pDrawList = ImGui::GetWindowDrawList();
    const ImU32 uGridColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.3f, 0.3f));

    // Horizontal lines
    for (int i = 0; i <= iGridLines; ++i)
    {
        const float fY = vGraphMin.y + ((vGraphMax.y - vGraphMin.y) * i / iGridLines);
        pDrawList->AddLine(
            ImVec2(vGraphMin.x, fY),
            ImVec2(vGraphMax.x, fY),
            uGridColor
        );
    }

    // Vertical lines
    for (int i = 0; i <= iGridLines; ++i)
    {
        const float fX = vGraphMin.x + ((vGraphMax.x - vGraphMin.x) * i / iGridLines);
        pDrawList->AddLine(
            ImVec2(fX, vGraphMin.y),
            ImVec2(fX, vGraphMax.y),
            uGridColor
        );
    }
}
