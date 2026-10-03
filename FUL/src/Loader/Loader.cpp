#include "Loader.h"
#include "Auth/Auth.h"
#include "Web/Web.h"
#include "Zip/Zip.h"
#include "Bypass/Bypass.h"
#include "Injector/LoadLibrary/LoadLibrary.h"
#include "Injector/ManualMap/ManualMap.h"
#include "../Utils/Utils.h"
#include "../../resource.h"
#include "../Utils/FULVM.h"
#include "../Utils/xorstr.hpp"

#include <filesystem>
#include <stdexcept>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

constexpr WORD ZIP_SIGNATURE = 0x4B50;

// How long we wait for the TF2 client to finish loading (ServerBrowser.dll)
// before starting injection once the process itself has been found.
constexpr DWORD kClientWaitSeconds = 120;

// How long the freshly injected game must stay alive and responsive before the
// loader reports success. A crash or a hung window with a frozen working set
// inside this window triggers a fresh-process retry.
constexpr DWORD kVerifySeconds = 8;

// Max times we relaunch a fresh game process after a crash/hang. The loader
// gives up (and surfaces a failure) after this many failed attempts.
constexpr int kMaxInjectAttempts = 3;

// Staged self-update command, populated by CheckForUpdate()
static std::wstring g_PendingUpdateCmd;

namespace
{
	// ANSI XorStr + widen helpers: the wide XorStrW template trips an MSVC ICE.
	// Hoisted statics so the VM-protected cores reference plain function calls.
	const std::wstring& NewExeName()
	{
		static const std::string s = XorStr("Synapse.exe.new");
		static const std::wstring w(s.begin(), s.end());
		return w;
	}

	const std::wstring& UpdaterCmdName()
	{
		static const std::string s = XorStr("Synapse-update.cmd");
		static const std::wstring w(s.begin(), s.end());
		return w;
	}

	const std::wstring& SynapseExeName()
	{
		static const std::string s = XorStr("Synapse.exe");
		static const std::wstring w(s.begin(), s.end());
		return w;
	}

	const std::wstring& SynapseName()
	{
		static const std::string s = XorStr("Synapse");
		static const std::wstring w(s.begin(), s.end());
		return w;
	}

	const std::filesystem::path& SynapseDir()
	{
		static const std::filesystem::path p = SynapseName();
		return p;
	}
}

// ---------------------------------------------------------------------------
// Themida VM regions. Every core is a single straight-line block (one entry,
// one exit, no return/throw before the matching `_END`) and only calls
// unmarked code. One named VM family per logical part.
// ---------------------------------------------------------------------------
namespace
{
	// SHARK_BLACK: extracts the PE TimeDateStamp (build time, seconds since 1970).
	static __declspec(noinline) DWORD build_stamp_core(const Binary& binary)
	{
		VM_SHARK_BLACK_START;
		DWORD stamp = 0;
		if (binary.size() >= sizeof(IMAGE_DOS_HEADER))
		{
			const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(binary.data());
			if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE)
			{
				if (dosHeader->e_lfanew >= 0 && dosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS32) <= static_cast<int>(binary.size()))
				{
					const auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(binary.data() + dosHeader->e_lfanew);
					if (ntHeaders->Signature == IMAGE_NT_SIGNATURE)
					{
						stamp = ntHeaders->FileHeader.TimeDateStamp;
					}
				}
			}
		}
		VM_SHARK_BLACK_END;
		return stamp;
	}

	// DOLPHIN_WHITE: compares two build stamps. 0 up-to-date, 1 server-newer,
	// 2 local-newer (dev build), 3 invalid.
	static __declspec(noinline) int update_verdict_core(DWORD localStamp, DWORD remoteStamp)
	{
		VM_DOLPHIN_WHITE_START;
		int verdict = 0;
		if (localStamp == 0 || remoteStamp == 0) { verdict = 3; }
		else if (remoteStamp == localStamp) { verdict = 0; }
		else if (remoteStamp < localStamp) { verdict = 2; }
		else { verdict = 1; }
		VM_DOLPHIN_WHITE_END;
		return verdict;
	}

	// DOLPHIN_BLACK: detects a zipped payload and unpacks it in place.
	static __declspec(noinline) Binary binary_sniff_core(const Binary& binary)
	{
		VM_DOLPHIN_BLACK_START;
		Binary out;
		bool packed = false;
		if (binary.size() >= sizeof(IMAGE_DOS_HEADER))
		{
			const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(binary.data());
			if (dosHeader->e_magic == ZIP_SIGNATURE) { packed = true; }
		}
		if (packed) { out = Zip::UnpackFile(binary); }
		else { out = binary; }
		VM_DOLPHIN_BLACK_END;
		return out;
	}

	// DOLPHIN_RED: spawns the staged updater script hidden.
	static __declspec(noinline) bool updater_launch_core(const std::wstring& cmd)
	{
		VM_DOLPHIN_RED_START;
		bool launched = false;
		STARTUPINFOW startupInfo = {};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo = {};
		wchar_t* mutableCmd = const_cast<wchar_t*>(cmd.c_str());
		if (CreateProcessW(nullptr, mutableCmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo))
		{
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			launched = true;
		}
		VM_DOLPHIN_RED_END;
		return launched;
	}

	// FISH_BLACK: writes the new executable + waiting relaunch script into
	// %TEMP% and arms g_PendingUpdateCmd. No-op unless both writes succeed.
	static __declspec(noinline) bool update_stage_core(const wchar_t* exePath, const Binary& newest, const std::wstring& args)
	{
		VM_FISH_BLACK_START;
		bool staged = false;
		WCHAR tempPath[MAX_PATH];
		if (GetTempPathW(MAX_PATH, tempPath) != 0)
		{
			const std::filesystem::path newFile = std::filesystem::path(tempPath) / SynapseDir() / NewExeName();
			const std::filesystem::path updater = std::filesystem::path(tempPath) / SynapseDir() / UpdaterCmdName();
			if (Utils::WriteBinaryFile(newFile.wstring(), newest))
			{
				const std::wstring newArg = newFile.wstring();
				const std::wstring script =
					L"@echo off\r\n"
					L"set \"TARGET=" + std::wstring(exePath) + L"\"\r\n"
					L"set \"SOURCE=" + newArg + L"\"\r\n"
					L"set \"ARGS=" + args + L"\"\r\n"
					L":loop\r\n"
					L"tasklist /fi \"imagename eq " + SynapseExeName() + L"\" | findstr /i \"" + SynapseName() + L"\" >nul 2>&1\r\n"
					L"if not errorlevel 1 (\r\n"
					L"  timeout /t 1 /nobreak >nul 2>&1\r\n"
					L"  goto loop\r\n"
					L")\r\n"
					L"copy /y \"%SOURCE%\" \"%TARGET%\" >nul 2>&1\r\n"
					L"start \"\" \"%TARGET%\" %ARGS%\r\n"
					L"del /f /q \"%~f0\" >nul 2>&1\r\n"
					L"exit /b\r\n";
				const int length = WideCharToMultiByte(CP_ACP, 0, script.c_str(), static_cast<int>(script.size()), nullptr, 0, nullptr, nullptr);
				std::string ansi;
				if (length > 0)
				{
					ansi.assign(static_cast<size_t>(length), '\0');
					WideCharToMultiByte(CP_ACP, 0, script.c_str(), static_cast<int>(script.size()), ansi.data(), length, nullptr, nullptr);
				}
				const Binary scriptBytes(ansi.begin(), ansi.end());
				if (Utils::WriteBinaryFile(updater.wstring(), scriptBytes))
				{
					g_PendingUpdateCmd = L"cmd.exe /c \"" + updater.wstring() + L"\"";
					staged = true;
				}
			}
		}
		VM_FISH_BLACK_END;
		return staged;
	}

	// EAGLE_WHITE: clamps a negative product selector to the first payload.
	// The upper bound is validated against the sidecar list at load time, so
	// the VM core only guards against the uninitialized (negative) default.
	static __declspec(noinline) int product_index_core(int idx)
	{
		VM_EAGLE_WHITE_START;
		int out = idx;
		if (out < 0) { out = 0; }
		VM_EAGLE_WHITE_END;
		return out;
	}

	// EAGLE_RED: minimum sanity check for an injectable image.
	static __declspec(noinline) bool binary_validate_core(const Binary& binary)
	{
		VM_EAGLE_RED_START;
		const bool valid = binary.size() >= 0x1000;
		VM_EAGLE_RED_END;
		return valid;
	}

	// Unmarked helper: matches a visible top-level window that belongs to `pid`.
struct GameWindowMatch { DWORD pid; HWND hwnd; };
static BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lParam)
{
	auto* ctx = reinterpret_cast<GameWindowMatch*>(lParam);
	if (!ctx || ctx->hwnd) { return TRUE; }

	DWORD windowPid = 0;
	GetWindowThreadProcessId(hwnd, &windowPid);
	if (windowPid == ctx->pid && IsWindowVisible(hwnd))
	{
		ctx->hwnd = hwnd;
		return FALSE;
	}
	return TRUE;
}

// Finds a visible top-level window owned by `pid` so the VM core can probe
// message responsiveness. Windowless/different-DPI games degrade gracefully:
// the health core only treats a process as hung when nothing responds AND its
// working set is completely static.
static HWND FindGameWindow(DWORD pid)
{
	GameWindowMatch ctx = { pid, nullptr };
	EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&ctx));
	return ctx.hwnd;
}

// LION_BLACK: watches a freshly injected game for `windowSeconds` seconds and
// reports whether it is truly healthy. 0 = crashed/exited, 1 = responsive,
// 2 = alive but hung (unresponsive window with a completely frozen working
// set, e.g. after destabilising injection).
static __declspec(noinline) int game_health_core(DWORD pid, HWND gameWindow, DWORD windowSeconds, DWORD tickMs)
{
	VM_LION_BLACK_START;
	int verdict = 0;
	bool opened = false;
	bool alive = true;
	bool movedOnce = false;
	bool responsiveOnce = false;
	SIZE_T lastWs = 0;
	HANDLE hGame = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (hGame)
	{
		opened = true;
		const auto deadline = GetTickCount64() + static_cast<ULONGLONG>(windowSeconds) * 1000;
		while (alive && GetTickCount64() < deadline)
		{
			DWORD exitCode = 0;
			if (!GetExitCodeProcess(hGame, &exitCode) || exitCode != STILL_ACTIVE) { alive = false; break; }

			PROCESS_MEMORY_COUNTERS pmc = {};
			if (GetProcessMemoryInfo(hGame, &pmc, sizeof(pmc)))
			{
				if (lastWs != 0 && pmc.WorkingSetSize != lastWs) { movedOnce = true; }
				lastWs = pmc.WorkingSetSize;
			}

			if (gameWindow)
			{
				DWORD_PTR probe = 0;
				const bool responded = SendMessageTimeoutW(gameWindow, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &probe);
				if (responded) { responsiveOnce = true; }
			}

			if (GetTickCount64() + tickMs >= deadline) { break; }
			Sleep(tickMs);
		}
		CloseHandle(hGame);
	}

	if (!opened || !alive)
	{
		verdict = 0;
	}
	else if (responsiveOnce || movedOnce)
	{
		verdict = 1;
	}
	else
	{
		verdict = 2;
	}
	VM_LION_BLACK_END;
	return verdict;
}
} // namespace (Themida VM regions)

// Resolves the payload for the selected product: Framework (id 0) is always
// the bundled Synapse.dll; ids 1..N pick the DLLs found next to the loader
// executable (see Utils::EnumSidecarPayloads), again falling back to the
// embedded Synapse.dll when the sidecar is missing or unreadable.
Binary GetBundledDll(const LaunchInfo& launchInfo)
{
	const int idx = product_index_core(launchInfo.Product);

	if (idx != 0)
	{
		const auto sidecars = Utils::EnumSidecarPayloads();
		const int sel = idx - 1;

		if (sel >= static_cast<int>(sidecars.size()))
		{
			Log::Warn("Product index {} out of range ({} payloads), using bundled Synapse.dll",
					  launchInfo.Product, sidecars.size());
		}
		else
		{
			const std::wstring& sidecar = sidecars[sel];
			const auto exists = std::filesystem::exists(sidecar);
			if (exists)
			{
				Log::Info(L"Loading payload '{}' from next to the loader...", sidecar);
				Binary binary = Utils::ReadBinaryFile(sidecar);
				if (!binary.empty())
				{
					return binary;
				}
				Log::Warn(L"Payload '{}' exists but could not be read, falling back to bundled Synapse.dll", sidecar);
			}
			else
			{
				Log::Info(L"Payload '{}' not found next to the loader, using bundled Synapse.dll", sidecar);
			}
		}
	}

	return Utils::GetBinaryResource(IDR_SYNAPSE_DLL);
}

// Default cheat file: Synapse.dll next to the loader executable
std::wstring GetDefaultDllPath()
{
	WCHAR path[MAX_PATH];
	if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
	{
		throw std::runtime_error("Failed to retrieve loader path");
	}

	const std::filesystem::path exePath(path);
	return (exePath.parent_path() / L"Synapse.dll").wstring();
}

// Embedded GH Injector runtime (exe + Qt5 dlls + plugins). The ntdll PDB files
// are fetched by GH itself from the Microsoft symbol server on first run.
struct GhResource
{
	WORD Id;
	LPCWSTR RelativePath;
};

// Embedded GH runtime table (exe + Qt5 dlls + plugins). The ntdll PDB files
// are fetched by GH itself from the Microsoft symbol server on first run.
constexpr GhResource g_GhResources[] = {
	{ IDR_GH_EXE,        L"GH Injector - x64.exe" },
	{ IDR_GH_DLL,        L"GH Injector - x64.dll" },
	{ IDR_GH_SM86,       L"GH Injector SM - x86.exe" },
	{ IDR_GH_QT5CORE,    L"Qt5Core.dll" },
	{ IDR_GH_QT5GUI,     L"Qt5Gui.dll" },
	{ IDR_GH_QT5WIDGETS, L"Qt5Widgets.dll" },
	{ IDR_GH_QWINDOWS,   L"platforms\\qwindows.dll" },
	{ IDR_GH_QJPEG,      L"imageformats\\qjpeg.dll" },
};

// Extracts the bundled GH Injector runtime into %TEMP%\Synapse\gh.
std::wstring ExtractGhRuntime()
{
	WCHAR tempPath[MAX_PATH];
	if (GetTempPathW(MAX_PATH, tempPath) == 0)
	{
		throw std::runtime_error("Failed to get temp path");
	}

	const std::filesystem::path baseDir = std::filesystem::path(tempPath) / SynapseDir() / L"gh";
	for (const auto& res : g_GhResources)
	{
		const auto target = baseDir / res.RelativePath;

		// Skip when an extracted copy with a matching size already exists
		std::error_code ec;
		const auto existingSize = std::filesystem::file_size(target, ec);
		if (!ec && existingSize > 0) { continue; }

		std::filesystem::create_directories(target.parent_path(), ec);
		const Binary data = Utils::GetBinaryResource(res.Id);
		if (!Utils::WriteBinaryFile(target.wstring(), data))
		{
			throw std::runtime_error("Failed to extract GH Injector runtime");
		}
	}

	Log::Info(L"GH: Extracted runtime to '{}'", baseDir.wstring());
	return (baseDir / L"GH Injector - x64.exe").wstring();
}

// Locates the GuidedHacking Injector next to the loader executable, falling
// back to the embedded runtime when it is not present.
bool FindGhInjector(std::wstring& out)
{
	WCHAR path[MAX_PATH];
	if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
	{
		return false;
	}

	const std::filesystem::path exePath(path);
	const std::filesystem::path candidates[] = {
		exePath.parent_path() / L"GH Injector - x64.exe",
		exePath.parent_path() / L"GH" / L"GH Injector - x64.exe"
	};

	for (const auto& candidate : candidates)
	{
		if (std::filesystem::exists(candidate))
		{
			out = candidate.wstring();
			return true;
		}
	}

	// Use the embedded copy
	out = ExtractGhRuntime();
	return true;
}

// Removes the temporary files GH operated on once it has finished.
void CleanupGhTemp(const std::wstring& dllPath)
{
	std::error_code ec;

	// The staged DLL copy
	if (!dllPath.empty()) { std::filesystem::remove(dllPath, ec); }

	// The embedded runtime (extracted exe, Qt dlls and plugins)
	WCHAR tempPath[MAX_PATH];
	if (GetTempPathW(MAX_PATH, tempPath) != 0)
	{
		const auto ghDir = std::filesystem::path(tempPath) / SynapseDir() / L"gh";
		std::filesystem::remove_all(ghDir, ec);
	}

	Log::Debug("GH: Cleaned up temporary runtime");
}

// Runs the GuidedHacking Injector against tf_win64.exe using the same proven
// flag set we use for testing: packer-friendly manual mapping, thread cloaking,
// PEB/header handling, memory mapping and a DllMain wait.
bool RunGhInjector(const std::wstring& ghPath, const std::wstring& dllPath, bool debug)
{
	// -silent makes GH hide and (from testing) abort the injection, so it is
	// never used. The window is instead kept hidden via STARTUPINFO so a mapped
	// run behaves identically to a debug run without flashing a console.
	std::wstring cmdLine = std::format(
		L"\"{:s}\" -p tf_win64.exe -f \"{:s}\" -timeout 2000 -log -l 4 -peh 2 -cloak -random -copy -mmflags 5FE0000 -wait",
		ghPath, dllPath);

	Log::Info(L"GH: {:s}", cmdLine);

	std::filesystem::path exeDir = std::filesystem::path(ghPath).parent_path();
	STARTUPINFOW startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION processInfo = {};
	wchar_t* mutableCmd = const_cast<wchar_t*>(cmdLine.c_str());

	if (!CreateProcessW(nullptr, mutableCmd, nullptr, nullptr, FALSE, debug ? 0 : CREATE_NO_WINDOW, nullptr, exeDir.c_str(), &startupInfo, &processInfo))
	{
		throw std::runtime_error(std::format("Failed to launch GH Injector (0x{:X})", GetLastError()));
	}

	Log::Info("GH: Waiting for injection to finish...");
	WaitForSingleObject(processInfo.hProcess, 60 * 1000);

	DWORD exitCode = 0;
	GetExitCodeProcess(processInfo.hProcess, &exitCode);

	// Tidy up the temp artifacts GH no longer needs after it has exited
	CleanupGhTemp(dllPath);

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);

	if (exitCode != 0)
	{
		throw std::runtime_error(std::format("GH Injector exited with code {:X}", exitCode));
	}

	Log::Info("GH: Done!");
	return true;
}

// Reads the PE header TimeDateStamp (build time, seconds since 1970) that the
// linker writes when the executable is linked.
DWORD GetBuildTimestamp(const Binary& binary)
{
	return build_stamp_core(binary);
}

// Downloads the newest loader executable and stages a self-update. The staged
// copy is applied by ApplyPendingUpdate() once this process exits. No-op when
// the cached build mode is enabled (offline mode).
bool Loader::CheckForUpdate(const LaunchInfo& launchInfo)
{
	if (launchInfo.Cache) { return false; }

	WCHAR exePath[MAX_PATH];
	if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) { return false; }

	Log::Info("Updater: Checking for a new loader...");

	Binary newest;
	try
	{
		newest = Web::DownloadFile(std::wstring(Auth::LoaderExeUrl().begin(), Auth::LoaderExeUrl().end()));
	}
	catch (const std::exception& ex)
	{
		Log::Warn("Updater: Download failed: {}", ex.what());
	}
	if (newest.size() < 0x1000) { return false; }

	Binary current;
	try { current = Utils::ReadBinaryFile(exePath); }
	catch (...) { return false; }

	// Compare the linker-stamped build timestamps (seconds since 1970):
	// older -> update, same -> up to date, newer -> development build (ignore)
	const DWORD localStamp = build_stamp_core(current);
	const DWORD remoteStamp = build_stamp_core(newest);
	Log::Info("Updater: Local build {}, server build {}", localStamp, remoteStamp);

	const int verdict = update_verdict_core(localStamp, remoteStamp);
	if (verdict == 3)
	{
		Log::Warn("Updater: Invalid build timestamps ({} / {})", localStamp, remoteStamp);
		return false;
	}
	if (verdict == 0)
	{
		Log::Info("Updater: Loader is up to date");
		return false;
	}
	if (verdict == 2)
	{
		Log::Info("Updater: Local build is newer - assuming a development build, ignoring server build");
		return false;
	}

	Log::Info("Updater: Server build is newer by {:d} seconds, staging update", remoteStamp - localStamp);

	// Preserve the original command line arguments for the relaunch
	std::wstring args;
	{
		int nArgs = 0;
		LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
		if (szArglist)
		{
			for (int i = 1; i < nArgs; i++)
			{
				args += L" \"";
				args += szArglist[i];
				args += L"\"";
			}
			LocalFree(szArglist);
		}
	}

	if (!update_stage_core(exePath, newest, args))
	{
		return false;
	}

	WCHAR tempPath[MAX_PATH];
	if (GetTempPathW(MAX_PATH, tempPath) != 0)
	{
		const std::wstring newArg = (std::filesystem::path(tempPath) / SynapseDir() / NewExeName()).wstring();
		Log::Info(L"Updater: New loader staged ({} -> {})", newArg, std::wstring(exePath));
	}
	return true;
}

// Launches the staged updater. It waits for this process to exit, replaces the
// executable and relaunches with the original arguments. Call right before exit.
void Loader::ApplyPendingUpdate()
{
	if (g_PendingUpdateCmd.empty()) { return; }

	if (updater_launch_core(g_PendingUpdateCmd))
	{
		Log::Info("Updater: Update will be applied on exit");
	}
	else
	{
		Log::Error("Updater: Failed to launch updater (0x{:X})", GetLastError());
	}
}

// Retrieves the Synapse binary from web/disk
Binary GetBinary(const LaunchInfo& launchInfo)
{
	Binary binary;

	if (launchInfo.Cache)
	{
		// Cached build: skip the update check and use the local file
		const auto& file = launchInfo.File.empty() ? GetDefaultDllPath() : launchInfo.File;
		Log::Info(L"Cache build enabled, using local file '{}'...", file);
		binary = Utils::ReadBinaryFile(file);
	}
	else
	{
		// Use an explicitly provided URL, otherwise the bundled copy
		if (!launchInfo.URL.empty())
		{
			Log::Info(L"Downloading file '{}'...", launchInfo.URL);
			binary = Web::DownloadFile(launchInfo.URL);
		}
		else if (launchInfo.File.empty())
		{
			try
			{
				binary = GetBundledDll(launchInfo);
			}
			catch (const std::exception& ex)
			{
				Log::Error("Failed to load bundled payload: {}", ex.what());
			}
		}

		if (binary.empty())
		{
			const auto& file = launchInfo.File.empty() ? GetDefaultDllPath() : launchInfo.File;
			Log::Warn(L"Remote download empty/failed, falling back to local file '{}'...", file);
			binary = Utils::ReadBinaryFile(file);
		}
	}

	// Check if the file is packed
	bool zipped = false;
	if (binary.size() >= sizeof(IMAGE_DOS_HEADER))
	{
		const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(binary.data());
		zipped = dosHeader->e_magic == ZIP_SIGNATURE;
	}
	if (zipped) { Log::Info("Zip file detected! Unpacking..."); }
	binary = binary_sniff_core(binary);

	Log::Info("Loaded {:d} bytes (SHA-256: {})", binary.size(), Utils::ComputeSHA256(binary));

	return binary;
}

void Loader::Run(const LaunchInfo& launchInfo)
{
	if (launchInfo.UseLL)
	{
		Debug(launchInfo);
	}
	else
	{
		Load(launchInfo);
	}
}

// Loads and injects Synapse.
//
// The game is injected up to kMaxInjectAttempts times. Each attempt waits for
// a fresh tf_win64.exe, waits for the client main menu, injects and then
// verifies the process is genuinely healthy (alive, responsive, working set
// moving). If the game crashes or hangs (e.g. a frozen working set after a
// destabilising injection) we kill the task and relaunch a fresh process for
// the next attempt, rather than leaving the user on a dead client.
bool Loader::Load(const LaunchInfo& launchInfo, const Loader::PhaseCallback& onPhase)
{
	// Retrieve the binary
	const Binary binary = GetBinary(launchInfo);
	if (!binary_validate_core(binary))
	{
		throw std::runtime_error("Invalid binary file");
	}

	// Restart Steam/TF2, inject and verify the VAC Bypass module. Any failure
	// here propagates up: every bypassed launch must proceed verified.
	// Modes: no args = bypass only if not already active, -secure = force it,
	// -nobypass = skip entirely (user brings their own bypass).
	if (!launchInfo.NoBypass)
	{
		Log::Info("Launch: VAC bypass requested (secure = {})", launchInfo.Secure);
		if (onPhase) { onPhase(Phase::WaitingForSteam); }

		Bypass::Run(launchInfo.Secure);

		Log::Info("Launch: bypass step complete and verified");
		if (onPhase) { onPhase(Phase::EnablingBypass); }
	}
	else
	{
		Log::Info("VAC Bypass skipped by choice - VAC is NOT bypassed");
	}

	// Prefer the GuidedHacking Injector (packer-friendly), falling back to the
	// built-in manual mapper when the GH executable is not available.
	std::wstring ghPath = launchInfo.GHPath;
	if (ghPath.empty() && !launchInfo.NoGH)
	{
		FindGhInjector(ghPath);
	}

	for (int attempt = 1; attempt <= kMaxInjectAttempts; ++attempt)
	{
		Log::Info("Launch: attempt {} of {}", attempt, kMaxInjectAttempts);

		if (attempt > 1)
		{
			// Previous attempt left the game crashed or frozen - kill it and
			// relaunch a completely fresh process for the new attempt.
			Log::Warn("Launch: killing unhealthy game and relaunching a fresh process");
			Utils::WaitCloseProcess("tf_win64.exe", 15);
			Utils::WaitCloseProcess("hl2.exe", 15);
			Sleep(1000);
			Bypass::LaunchGame();
		}

		// Find the game
		Log::Info("Waiting for game...");
		if (onPhase) { onPhase(Phase::WaitingForGame); }
		const HANDLE hGame = Utils::WaitForProcessHandle("tf_win64.exe", 90);
		if (hGame == INVALID_HANDLE_VALUE || hGame == nullptr)
		{
			Log::Warn("Launch: timeout waiting for game (attempt {})", attempt);
			continue;
		}
		Log::Info("Launch: game found (tf_win64.exe)");

		// Do not inject yet: ServerBrowser.dll is the last module the TF2
		// client maps before the main menu is ready. Wait for it so the cheat
		// is applied to a fully loaded client.
		Log::Info("Launch: waiting for client to finish loading (ServerBrowser.dll)...");
		if (onPhase) { onPhase(Phase::WaitingForClient); }

		const DWORD gamePid = GetProcessId(hGame);
		if (!Utils::WaitForModule(gamePid, "ServerBrowser.dll", kClientWaitSeconds))
		{
			CloseHandle(hGame);
			Log::Warn("Launch: timeout waiting for client to load (attempt {})", attempt);
			continue;
		}
		Log::Info("Launch: client ready (ServerBrowser.dll detected)");

		if (onPhase) { onPhase(Phase::Loading); }

		bool injected = false;
		if (!ghPath.empty())
		{
			// GH Injector injects from a file on disk. The game handle is no
			// longer needed for this path (GH resolves the process itself);
			// close it before GH may throw so the handle never leaks.
			CloseHandle(hGame);

			WCHAR tempPath[MAX_PATH];
			if (GetTempPathW(MAX_PATH, tempPath) == 0)
			{
				throw std::runtime_error("Failed to get temp path");
			}

			const std::wstring dllPath = (std::filesystem::path(tempPath) / L"Synapse.dll").wstring();
			if (!Utils::WriteBinaryFile(dllPath, binary))
			{
				throw std::runtime_error("Failed to write binary to temp file");
			}

			Log::Info(L"GH: Injecting '{}'...", dllPath);
			injected = RunGhInjector(ghPath, dllPath, launchInfo.Debug);
		}
		else
		{
			// Inject the binary
			Log::Info("Manual mapping {:d} bytes...", binary.size());
			injected = MM::Inject(hGame, binary);
			CloseHandle(hGame);
		}

		if (!injected)
		{
			Log::Warn("Launch: injection failed (attempt {})", attempt);
			continue;
		}

		// Confirm the game survived injection and is actually usable. A crash
		// or a frozen working set (unresponsive window, static memory) means
		// the attempt destabilised the client - retry with a fresh process.
		const HWND gameWindow = FindGameWindow(gamePid);
		const int verdict = game_health_core(gamePid, gameWindow, kVerifySeconds, 250);
		if (verdict == 1)
		{
			Log::Info("Launch: game healthy after injection (attempt {})", attempt);
			if (onPhase) { onPhase(Phase::Done); }
			return true;
		}

		Log::Warn("Launch: game {} after injection (attempt {})",
			verdict == 0 ? "crashed/exited" : "hung (unresponsive, frozen working set)", attempt);
	}

	throw std::runtime_error(std::format("Game failed to stay healthy after {} injection attempts", kMaxInjectAttempts));
}

bool Loader::Debug(const LaunchInfo& launchInfo, const Loader::PhaseCallback& onPhase)
{
	if (onPhase) { onPhase(Phase::WaitingForGame); }

	// Find the game
	Log::Info("Waiting for game...");
	const HANDLE hGame = Utils::GetProcessHandle("tf_win64.exe");
	if (hGame == INVALID_HANDLE_VALUE || hGame == nullptr)
	{
		throw std::runtime_error("Failed to get game handle");
	}

	if (onPhase) { onPhase(Phase::Loading); }

	// LoadLibrary requires a file on disk: resolve the binary and stash it in %TEMP%
	std::wstring dllPath = launchInfo.File;
	if (dllPath.empty())
	{
		Binary binary;
		if (!launchInfo.URL.empty())
		{
			Log::Info(L"Downloading '{}'...", launchInfo.URL);
			try { binary = Web::DownloadFile(launchInfo.URL); }
			catch (const std::exception& ex) { Log::Error("Download failed: {}", ex.what()); }
		}
		else
		{
			try { binary = GetBundledDll(launchInfo); }
			catch (const std::exception& ex) { Log::Error("Failed to load bundled payload: {}", ex.what()); }
		}

		if (binary.size() < 0x1000)
		{
			dllPath = GetDefaultDllPath();
		}
		else
		{
			WCHAR tempPath[MAX_PATH];
			if (GetTempPathW(MAX_PATH, tempPath) == 0)
			{
				throw std::runtime_error("Failed to get temp path");
			}

			dllPath = (std::filesystem::path(tempPath) / L"Synapse.dll").wstring();
			if (!Utils::WriteBinaryFile(dllPath, binary))
			{
				throw std::runtime_error("Failed to write binary to temp file");
			}

			Log::Info("SHA-256: {}", Utils::ComputeSHA256(binary));
		}
	}

	// Inject the binary
	Log::Info(L"Running LoadLibrary with file '{}'...", dllPath);
	const bool ok = LL::Inject(hGame, dllPath.c_str());
	if (onPhase) { onPhase(Phase::Done); }
	return ok;
}
