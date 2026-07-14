#pragma once

#include <Windows.h>

// M3-TEXTURE-ASYNC-10-RUNTIME: System Memory Detection and Budget Management
// Automatically detects system RAM and recommends texture memory budget
// Provides dynamic adjustment based on available memory

class CSystemMemoryDetector
{
public:
	// Detect optimal texture budget based on total physical RAM
	// Returns: Recommended texture budget in MB
	// Categories:
	//   < 4GB RAM   -> 512MB budget
	//   4-8GB RAM   -> 1024MB budget (1GB)
	//   8-16GB RAM  -> 2048MB budget (2GB)
	//   > 16GB RAM  -> 4096MB budget (4GB)
	static DWORD DetectOptimalTextureBudgetMB();

	// Get total physical memory in MB
	static DWORD GetTotalPhysicalMemoryMB();

	// Get currently available physical memory in MB
	static DWORD GetAvailablePhysicalMemoryMB();

	// Get recommended safety margin (25% of total RAM)
	static DWORD GetSafetyMarginMB();

	// Check if memory is under pressure and adjust budget if needed
	// Call this periodically (every 30s recommended)
	// Returns: true if budget was adjusted, false otherwise
	static bool AdjustBudgetIfNeeded();

	// Check if system memory is under pressure
	// Returns: true if available memory < 20% of total
	static bool IsMemoryUnderPressure();

private:
	// Last detected values (cached)
	static DWORD ms_dwTotalPhysicalMemoryMB;
	static DWORD ms_dwLastBudgetMB;
	static bool ms_bInitialized;

	// Update cached memory info
	static void __UpdateMemoryInfo();
};
