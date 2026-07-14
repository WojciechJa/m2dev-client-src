#include "StdAfx.h"
#include "SystemMemoryDetector.h"
#include "GrpTextureDX11.h"

// M3-TEXTURE-ASYNC-10-RUNTIME: System Memory Detection Implementation

// Static member initialization
DWORD CSystemMemoryDetector::ms_dwTotalPhysicalMemoryMB = 0;
DWORD CSystemMemoryDetector::ms_dwLastBudgetMB = 0;
bool CSystemMemoryDetector::ms_bInitialized = false;

void CSystemMemoryDetector::__UpdateMemoryInfo()
{
	MEMORYSTATUSEX memStatus;
	memStatus.dwLength = sizeof(MEMORYSTATUSEX);
	
	if (GlobalMemoryStatusEx(&memStatus))
	{
		// Convert bytes to MB
		ms_dwTotalPhysicalMemoryMB = static_cast<DWORD>(memStatus.ullTotalPhys / (1024 * 1024));
	}
	else
	{
		// Fallback: assume 4GB if detection fails
		ms_dwTotalPhysicalMemoryMB = 4096;
		TraceError("DX11_MEMORY_DETECT_FAIL reason=GlobalMemoryStatusEx_failed fallback_mb=4096");
	}
	
	ms_bInitialized = true;
}

DWORD CSystemMemoryDetector::GetTotalPhysicalMemoryMB()
{
	if (!ms_bInitialized)
		__UpdateMemoryInfo();
	
	return ms_dwTotalPhysicalMemoryMB;
}

DWORD CSystemMemoryDetector::GetAvailablePhysicalMemoryMB()
{
	MEMORYSTATUSEX memStatus;
	memStatus.dwLength = sizeof(MEMORYSTATUSEX);
	
	if (GlobalMemoryStatusEx(&memStatus))
	{
		return static_cast<DWORD>(memStatus.ullAvailPhys / (1024 * 1024));
	}
	
	// Fallback: assume 50% available
	return GetTotalPhysicalMemoryMB() / 2;
}

DWORD CSystemMemoryDetector::GetSafetyMarginMB()
{
	// Reserve 25% of total RAM for system
	return GetTotalPhysicalMemoryMB() / 4;
}

DWORD CSystemMemoryDetector::DetectOptimalTextureBudgetMB()
{
	DWORD dwTotalRAM = GetTotalPhysicalMemoryMB();
	DWORD dwBudget = 0;
	
	// Budget categories based on total physical RAM
	if (dwTotalRAM < 4096)
	{
		// < 4GB: Low-end system
		dwBudget = 512;
	}
	else if (dwTotalRAM < 8192)
	{
		// 4-8GB: Medium system
		dwBudget = 1024;
	}
	else if (dwTotalRAM < 16384)
	{
		// 8-16GB: High-end system
		dwBudget = 2048;
	}
	else
	{
		// > 16GB: Ultra system
		dwBudget = 4096;
	}
	
	// Apply safety margin: never use more than (total - safety_margin)
	DWORD dwMaxBudget = dwTotalRAM - GetSafetyMarginMB();
	if (dwBudget > dwMaxBudget)
	{
		dwBudget = dwMaxBudget;
		TraceError("DX11_TEXTURE_BUDGET_CLAMPED requested=%u max=%u total_ram=%u",
			dwBudget, dwMaxBudget, dwTotalRAM);
	}
	
	// Ensure minimum 256MB budget
	if (dwBudget < 256)
		dwBudget = 256;
	
	ms_dwLastBudgetMB = dwBudget;
	return dwBudget;
}

bool CSystemMemoryDetector::IsMemoryUnderPressure()
{
	DWORD dwAvailable = GetAvailablePhysicalMemoryMB();
	DWORD dwTotal = GetTotalPhysicalMemoryMB();
	
	// Memory under pressure if < 20% available
	DWORD dwPressureThreshold = dwTotal / 5;  // 20%
	
	return (dwAvailable < dwPressureThreshold);
}

bool CSystemMemoryDetector::AdjustBudgetIfNeeded()
{
	if (!ms_bInitialized)
	{
		DetectOptimalTextureBudgetMB();
		return false;
	}
	
	// Check if memory is under pressure
	if (!IsMemoryUnderPressure())
		return false;
	
	// Memory under pressure - reduce texture budget by 25%
	DWORD dwCurrentBudget = CGraphicTextureDX11::GetMemoryBudgetMB();
	DWORD dwNewBudget = (dwCurrentBudget * 3) / 4;  // Reduce by 25%
	
	// Ensure minimum 256MB
	if (dwNewBudget < 256)
		dwNewBudget = 256;
	
	// Only adjust if change is significant (> 10%)
	DWORD dwDifference = dwCurrentBudget > dwNewBudget ? 
		(dwCurrentBudget - dwNewBudget) : (dwNewBudget - dwCurrentBudget);
	
	if (dwDifference > (dwCurrentBudget / 10))
	{
		CGraphicTextureDX11::SetMemoryBudgetMB(dwNewBudget);
		
		TraceError("DX11_TEXTURE_BUDGET_ADJUSTED reason=memory_pressure old_mb=%u new_mb=%u available_mb=%u",
			dwCurrentBudget, dwNewBudget, GetAvailablePhysicalMemoryMB());
		
		ms_dwLastBudgetMB = dwNewBudget;
		return true;
	}
	
	return false;
}
