// SPDX-License-Identifier: MIT
// ImGuiManager.h - ImGui Developer Monitoring Tool Manager
// ImGui Developer Monitoring Tool - DX11 native only
//
// Integration Point: PythonApplication::CreateGroups()
// Hook Point: CGraphicDevice::Present() (DX11 only)
// Hotkey: F12 (configurable)

#pragma once
#include <d3d11.h>
#include <Windows.h>

#include "ImGuiMetricsCollector.h"
#include "ImGuiGraphicsMetrics.h"
#include "ImGuiGraphPlotter.h"

class CImGuiManager
{
public:
    // Singleton interface
    static CImGuiManager* Instance();
    static bool Create();
    static void Destroy();

    // Initialization (called from PythonApplication::CreateGroups)
    bool Initialize(HWND hWnd, ID3D11Device* pDX11Device,
                   ID3D11DeviceContext* pDX11Context);
    void Shutdown();

    // Frame management (called from CGraphicDevice::Present)
    void NewFrame();
    void RenderDX11Overlay();

    // Input handling (called from WndProc)
    bool HandleInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Toggle visibility
    void Toggle();
    bool IsEnabled() const { return m_bEnabled; }
    void SetEnabled(bool bEnable) { m_bEnabled = bEnable; }
    bool WantsMouseCapture() const { return m_bWantCaptureMouse; }
    bool WantsKeyboardCapture() const { return m_bWantCaptureKeyboard; }

    // Device management
    void InvalidateDeviceObjects();
    void CreateDeviceObjects();
    void OnResize(UINT width, UINT height);

    // Hotkey configuration
    void SetToggleKey(UINT key) { m_uiToggleKey = key; }
    UINT GetToggleKey() const { return m_uiToggleKey; }

    // Metrics update (called from PythonApplication::Loop)
    void UpdateMetrics(const SMetricsSnapshot& snapshot);

private:
    CImGuiManager();
    ~CImGuiManager();

    // Prevent copying
    CImGuiManager(const CImGuiManager&) = delete;
    CImGuiManager& operator=(const CImGuiManager&) = delete;

    // Initialization helpers
    bool InitializeImGuiContext();
    bool InitializeDX11Backend();
    bool InitializeWin32Backend();
    void SetupImGuiStyle();
    void RegisterDebugWindows();

    // Member variables
    bool m_bEnabled;
    bool m_bInitialized;
    bool m_bInitializedDX11;
    bool m_bInitializedWin32;

    HWND m_hWnd;
    UINT m_uiToggleKey;

    // DX11 Device pointers
    ID3D11Device* m_pDX11Device;
    ID3D11DeviceContext* m_pDX11Context;

    // Frame timing
    float m_fDeltaTime;
    LARGE_INTEGER m_lastTime;
    LARGE_INTEGER m_cpuFrequency;

    // Metrics snapshot (updated from PythonApplication::Loop)
    SMetricsSnapshot m_lastMetricsSnapshot;
    bool m_bWantCaptureMouse;
    bool m_bWantCaptureKeyboard;

    // Singleton instance
    static CImGuiManager* ms_pInstance;
};

// Global access macros
#define ImGuiMgr() CImGuiManager::Instance()
#define IsImGuiEnabled() (CImGuiManager::Instance() && CImGuiManager::Instance()->IsEnabled())
