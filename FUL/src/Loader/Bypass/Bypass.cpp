#include "Bypass.h"

#include "../Injector/ManualMap/ManualMap.h"
#include "../../Utils/Utils.h"
#include "../../Utils/xorstr.hpp"
#include "../../../resource.h"
#include "../../Utils/FULVM.h"

#include <stdexcept>
#include <format>

constexpr char STEAM_PID_SETTING[] = "SteamPID";

// The VAC-safe module creates this mutex while it lives inside Steam. It is
// the single source of truth for "the bypass is active": OpenMutexW succeeds
// only while the module holds it, so its presence proves Steam is running AND
// that our module got injected and survived. The name MUST match the one the
// prebuilt module creates (just-disable-bros-dll-tf-master's VAC-Bypass
// dllmain.c).
namespace
{
	// ANSI XorStr + widen: the wide XorStrW template trips an MSVC ICE.
	const std::wstring& VacMutexName()
	{
		static const std::string name = XorStr("Local\\FUL_VacBypass");
		static const std::wstring wide(name.begin(), name.end());
		return wide;
	}
}

// ---------------------------------------------------------------------------
// Themida VM regions. Single straight-line blocks, unmarked calls only.
// ---------------------------------------------------------------------------
namespace
{
	// PUMA_WHITE: tests the VAC bypass mutex without side effects on failure.
	static __declspec(noinline) bool bypass_active_core()
	{
		VM_PUMA_WHITE_START;
		bool active = false;
		HANDLE hMutex = ::OpenMutexW(SYNCHRONIZE, FALSE, VacMutexName().c_str());
		if (hMutex)
		{
			CloseHandle(hMutex);
			active = true;
		}
		VM_PUMA_WHITE_END;
		return active;
	}

	// PUMA_RED: true when the given PID is a running steam.exe client.
	static __declspec(noinline) bool steam_running_core(DWORD pid)
	{
		VM_PUMA_RED_START;
		bool running = false;
		if (pid != 0)
		{
			HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (hProcess)
			{
				wchar_t imagePath[MAX_PATH] = {};
				DWORD size = MAX_PATH;
				const BOOL ok = QueryFullProcessImageNameW(hProcess, 0, imagePath, &size);
				CloseHandle(hProcess);
				if (ok)
				{
					const wchar_t* name = wcsrchr(imagePath, L'\\');
					name = name ? name + 1 : imagePath;
					running = _wcsicmp(name, L"steam.exe") == 0;
				}
			}
		}
		VM_PUMA_RED_END;
		return running;
	}

	// PUMA_BLACK: reads the registered Steam client path. Empty when missing.
	static __declspec(noinline) std::wstring steam_path_core()
	{
		VM_PUMA_BLACK_START;
		std::wstring path;
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
		{
			WCHAR steamPath[MAX_PATH];
			DWORD bufferSize = sizeof(steamPath);
			if (RegQueryValueExW(hKey, L"SteamExe", nullptr, nullptr, reinterpret_cast<LPBYTE>(steamPath), &bufferSize) == ERROR_SUCCESS)
			{
				path = steamPath;
			}
			RegCloseKey(hKey);
		}
		VM_PUMA_BLACK_END;
		return path;
	}
}

void ExitSteam()
{
	Utils::WaitCloseProcess("tf_win64.exe");
	Utils::WaitCloseProcess("hl2.exe");
	Utils::WaitCloseProcess("steam.exe");
	Utils::WaitCloseProcess("SteamService.exe");
	Utils::WaitCloseProcess("steamwebhelper.exe");
}

// True when the given PID belongs to a currently running steam.exe
bool IsSteamProcessRunning(DWORD pid)
{
	return steam_running_core(pid);
}

std::wstring GetSteamPath()
{
	std::wstring path = steam_path_core();
	if (path.empty())
	{
		throw std::runtime_error("Failed to query the Steam client path");
	}
	return path;
}

bool IsBypassActive()
{
	return bypass_active_core();
}

// Waits up to `seconds` for the VAC-safe module's mutex to appear.
bool WaitForBypass(DWORD seconds)
{
	Log::Info("Bypass: waiting up to {:d}s for the VAC bypass mutex", seconds);
	const auto deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000;
	ULONGLONG lastReport = 0;
	while (GetTickCount64() < deadline)
	{
		if (IsBypassActive()) { return true; }
		const auto now = GetTickCount64();
		if (now - lastReport >= 5000)
		{
			lastReport = now;
			Log::Info("Bypass: VAC bypass mutex not seen yet ({:d}s elapsed)", static_cast<DWORD>((now - (deadline - static_cast<ULONGLONG>(seconds) * 1000)) / 1000));
		}
		Sleep(500);
	}
	const bool active = IsBypassActive();
	if (!active)
	{
		Log::Error("Bypass: VAC bypass mutex did NOT appear within {:d}s", seconds);
	}
	return active;
}

// Steam restarts itself during updates, so the PID CreateProcess returned can
// die while its replacement runs. Resolve the live client whatever its PID.
DWORD ResolveSteamProcess(DWORD createdPid)
{
	if (createdPid != 0 && IsSteamProcessRunning(createdPid)) { return createdPid; }
	return Utils::FindProcess("steam.exe");
}

// Launches TF2 through Steam. When the bypass is already active in Steam we
// keep Steam alive and only open the game, so injection still has a target.
bool Bypass::LaunchGame()
{
	// The game is already running - nothing to do
	if (Utils::FindProcess("tf_win64.exe") != 0)
	{
		Log::Info("Bypass: game is already running");
		return true;
	}

	const auto steamPath = GetSteamPath();
	const auto cmdLine = std::format(L"\"{:s}\" -applaunch 440", steamPath);

	STARTUPINFOW startupInfo = {};
	PROCESS_INFORMATION processInfo = {};
	if (!CreateProcessW(nullptr, const_cast<LPWSTR>(cmdLine.c_str()), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to launch Steam");
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);

	Log::Info("Bypass: Launched TF2 via Steam (-applaunch 440)");
	return true;
}

void Bypass::Run(bool force)
{
	// Default behaviour ("run as intended"): a verified bypass already living
	// in Steam is kept, and we just open TF2 through it. -secure forces the
	// full restart + re-inject even when a bypass is already present.
	if (!force && IsBypassActive())
	{
		Log::Info("Bypass: active VAC bypass found - keeping Steam alive, opening TF2");
		LaunchGame();
		return;
	}

	if (force)
	{
		Log::Info("Bypass: VACSAFE forced - restarting Steam and re-injecting anyway");
	}
	else
	{
		Log::Info("Bypass: no active VAC bypass found - performing a full restart");
	}

	// Close Steam and TF2
	Log::Info("Closing Steam & TF2...");
	ExitSteam();
	Sleep(1000);
	Log::Info("Bypass: Steam & TF2 closed");

	// Start Steam
	const auto steamPath = GetSteamPath();
	const auto cmdLine = std::format(L"\"{:s}\" -applaunch 440", steamPath);
	Log::Info(L"Bypass: launching {}", cmdLine);

	STARTUPINFOW startupInfo = {};
	PROCESS_INFORMATION processInfo = {};
	if (!CreateProcessW(nullptr, const_cast<LPWSTR>(cmdLine.c_str()), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to launch Steam");
	}
	Log::Info("Bypass: Steam launched (PID {:d})", processInfo.dwProcessId);

	// Wait for the client to come up, tolerating a PID change mid-flight in
	// case Steam restarts itself (e.g. auto-update) before it settles.
	Log::Info("Waiting for Steam...");
	DWORD steamPid = 0;
	const auto deadline = GetTickCount64() + 60ULL * 1000;
	while (GetTickCount64() < deadline)
	{
		steamPid = ResolveSteamProcess(processInfo.dwProcessId);
		if (steamPid != 0) { break; }
		Sleep(250);
	}
	if (steamPid == 0)
	{
		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
		throw std::runtime_error("Timeout while waiting for Steam");
	}
	Log::Info("Bypass: Steam is up (created PID {:d}, live PID {:d})", processInfo.dwProcessId, steamPid);

	// Let Steam reach a steady state before the module lands inside it.
	Sleep(2000);

	// Inject VAC Bypass into the live client.
	const bool createdPidAlive = steamPid == processInfo.dwProcessId;
	HANDLE hSteam = createdPidAlive ? processInfo.hProcess : OpenProcess(PROCESS_ALL_ACCESS, FALSE, steamPid);
	if (!hSteam)
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to open Steam process");
	}
	Log::Info("Bypass: injecting VAC module into Steam PID {:d}", steamPid);

	const Binary vacBypass = Utils::GetBinaryResource(IDR_VACBYPASS);
	const bool injected = MM::Inject(hSteam, vacBypass, createdPidAlive ? processInfo.hThread : nullptr);
	Log::Info("Bypass: manual map returned {}", injected ? "SUCCESS" : "FAILURE");

	if (!createdPidAlive) { CloseHandle(hSteam); }

	// Absolutely confirm the module is alive: its mutex must appear now.
	if (!WaitForBypass(20))
	{
		Log::Error("VAC Bypass did not activate inside Steam");
		throw std::runtime_error("VAC Bypass failed to activate inside Steam");
	}

	// Remember which Steam we injected, as a backup for the mutex check
	Utils::SetSettingDWord(STEAM_PID_SETTING, steamPid);

	Log::Info("VAC Bypass confirmed active in Steam (PID {:d})", steamPid);

	// Cleanup
	if (createdPidAlive) { ResumeThread(processInfo.hThread); }
	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);
}
