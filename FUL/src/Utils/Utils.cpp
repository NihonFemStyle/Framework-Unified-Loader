#include "Utils.h"

#include <fstream>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include "../../resource.h"
#include "FULVM.h"

// ---------------------------------------------------------------------------
// Themida VM regions. Every core is a single straight-line block (one entry,
// one exit, no return/throw before the `VM_END`) and only calls unmarked code.
// One family per logical part.
// ---------------------------------------------------------------------------
namespace
{
	// FISH_WHITE: snapshots the process list and matches an exe name.
	static __declspec(noinline) DWORD find_process_core(LPCSTR procName)
	{
		VM_FISH_WHITE_START;
		DWORD processId = 0;
		PROCESSENTRY32 procEntry{};
		procEntry.dwSize = sizeof(procEntry);
		const HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapshot != INVALID_HANDLE_VALUE)
		{
			if (Process32First(hSnapshot, &procEntry))
			{
				do
				{
					if (_strcmpi(procEntry.szExeFile, procName) == 0)
					{
						processId = procEntry.th32ProcessID;
						break;
					}
				}
				while (Process32Next(hSnapshot, &procEntry));
			}
			CloseHandle(hSnapshot);
		}
		VM_FISH_WHITE_END;
		return processId;
	}

	// FISH_RED: polls the module list of a process until a module is seen.
	static __declspec(noinline) bool wait_for_module_core(DWORD processId, LPCSTR moduleName, DWORD sTimeout)
	{
		VM_FISH_RED_START;
		const auto startTime = GetTickCount64();
		bool moduleFound = false;
		while (!moduleFound)
		{
			const HANDLE moduleSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
			if (moduleSnapshot != INVALID_HANDLE_VALUE)
			{
				MODULEENTRY32 moduleEntry;
				moduleEntry.dwSize = sizeof(moduleEntry);
				if (Module32First(moduleSnapshot, &moduleEntry))
				{
					do
					{
						if (!_strcmpi(moduleEntry.szModule, moduleName))
						{
							moduleFound = true;
							break;
						}
					}
					while (Module32Next(moduleSnapshot, &moduleEntry));
				}
				CloseHandle(moduleSnapshot);
			}
			Sleep(100);
			const auto currentTime = GetTickCount64();
			const auto elapsedTime = currentTime - startTime;
			if (elapsedTime >= static_cast<ULONGLONG>(sTimeout) * 1000) { break; }
		}
		VM_FISH_RED_END;
		return moduleFound;
	}

	// FISH_BLACK: queries the token elevation state of the current process.
	static __declspec(noinline) bool is_elevated_core()
	{
		VM_FISH_BLACK_START;
		bool elevated = false;
		HANDLE hToken = nullptr;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
		{
			TOKEN_ELEVATION elevation = {};
			DWORD dwSize = 0;
			if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize))
			{
				elevated = elevation.TokenIsElevated != 0;
			}
			CloseHandle(hToken);
		}
		VM_FISH_BLACK_END;
		return elevated;
	}

	// MUTATE_ONLY: writes a Synapse registry DWORD.
	static __declspec(noinline) void set_setting_dword_core(LPCSTR name, DWORD value)
	{
		MUTATE_START;
		HKEY hKey = nullptr;
		if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Synapse", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExA(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
			RegCloseKey(hKey);
		}
		MUTATE_END;
	}

	// FALCON_TINY: loads an embedded RCDATA blob.
	static __declspec(noinline) Binary binary_resource_core(WORD id)
	{
		VM_FALCON_TINY_START;
		Binary out;
		const HRSRC res = ::FindResource(nullptr, MAKEINTRESOURCE(id), RT_RCDATA);
		if (res)
		{
			const SIZE_T resSize = SizeofResource(nullptr, res);
			const HGLOBAL resData = LoadResource(nullptr, res);
			if (resData)
			{
				const auto binData = static_cast<BYTE*>(LockResource(resData));
				if (binData) { out.assign(binData, binData + resSize); }
			}
		}
		VM_FALCON_TINY_END;
		return out;
	}
}

DWORD Utils::FindProcess(LPCSTR procName)
{
	return find_process_core(procName);
}

HANDLE Utils::GetProcessHandle(LPCSTR procName)
{
	const DWORD pid = FindProcess(procName);
	if (pid == 0) { return nullptr; }

	const HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	return hProc;
}

DWORD Utils::WaitForProcess(LPCSTR procName, DWORD sTimeout)
{
	const auto startTime = GetTickCount64();
	DWORD processId = 0;

	while (processId == 0)
	{
		// Try to get the handle
		processId = FindProcess(procName);
		if (processId > 0) { return processId; }

		// Check timeout
		const auto currentTime = GetTickCount64();
		const auto elapsedTime = currentTime - startTime;
		if (elapsedTime >= static_cast<ULONGLONG>(sTimeout) * 1000) { break; }

		Sleep(100);
	}

	return 0;
}

HANDLE Utils::WaitForProcessHandle(LPCSTR procName, DWORD sTimeout)
{
	const auto startTime = GetTickCount64();
	HANDLE hProc = nullptr;

	while (!hProc)
	{
		// Try to get the handle
		hProc = GetProcessHandle(procName);
		if (hProc) { return hProc; }

		// Check timeout
		const auto currentTime = GetTickCount64();
		const auto elapsedTime = currentTime - startTime;
		if (elapsedTime >= static_cast<ULONGLONG>(sTimeout) * 1000) { break; }

		Sleep(100);
	}

	return nullptr;
}

bool Utils::WaitCloseProcess(LPCSTR procName, DWORD sTimeout)
{
	const HANDLE hProc = GetProcessHandle(procName);
	if (hProc == nullptr || hProc == INVALID_HANDLE_VALUE) { return false; }

	// Terminate and wait for exit
	if (!TerminateProcess(hProc, 0)) { return false; }
	const DWORD result = WaitForSingleObject(hProc, sTimeout * 1000);

	CloseHandle(hProc);
	return result == WAIT_OBJECT_0;
}

bool Utils::WaitForModule(DWORD processId, LPCSTR moduleName, DWORD sTimeout)
{
	return wait_for_module_core(processId, moduleName, sTimeout);
}

Binary Utils::ReadBinaryFile(const std::wstring& fileName)
{
	std::ifstream inFile(fileName, std::ios::binary | std::ios::ate);
	if (inFile.fail())
	{
		inFile.close();
		throw std::runtime_error("Failed to open file");
	}

	// Allocate a buffer
	const auto fileSize = static_cast<size_t>(inFile.tellg());
	Binary fileData(fileSize);

	// Read the file
	inFile.seekg(0, std::ios::beg);
	inFile.read(reinterpret_cast<char*>(fileData.data()), fileSize);
	inFile.close();

	return fileData;
}

bool Utils::WriteBinaryFile(const std::wstring& fileName, const Binary& data)
{
	std::ofstream outFile(fileName, std::ios::binary | std::ios::trunc);
	if (outFile.fail())
	{
		outFile.close();
		return false;
	}

	outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
	const bool success = !outFile.fail();
	outFile.close();
	return success;
}

// Every *.dll next to the loader executable is a selectable payload, except
// known support libraries. Sorted so the GUI and the loader agree on indices.
std::vector<std::wstring> Utils::EnumSidecarPayloads()
{
	std::vector<std::wstring> out;

	WCHAR exePath[MAX_PATH];
	if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
	{
		return out;
	}

	const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
	const std::wstring pattern = (exeDir / L"*.dll").wstring();
	const wchar_t* kSupportDlls[] = { L"SecureEngineSDK64.dll", L"SecureEngineSDK32.dll" };

	WIN32_FIND_DATAW fd{};
	const HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return out;
	}

	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			continue;
		}

		bool isSupport = false;
		for (const wchar_t* name : kSupportDlls)
		{
			if (_wcsicmp(fd.cFileName, name) == 0) { isSupport = true; break; }
		}
		if (isSupport)
		{
			continue;
		}

		out.emplace_back((exeDir / fd.cFileName).wstring());
	}
	while (FindNextFileW(hFind, &fd));

	FindClose(hFind);

	std::sort(out.begin(), out.end());
	return out;
}

std::string Utils::ComputeSHA256(const Binary& data)
{
	BCRYPT_ALG_HANDLE hAlg = nullptr;
	BCRYPT_HASH_HANDLE hHash = nullptr;
	BYTE hash[32] = {};

	const auto cleanup = [&]()
	{
		if (hHash) { BCryptDestroyHash(hHash); }
		if (hAlg) { BCryptCloseAlgorithmProvider(hAlg, 0); }
	};

	if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
	{
		return "";
	}
	if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) != 0)
	{
		cleanup();
		return "";
	}
	if (BCryptHashData(hHash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) != 0
		|| BCryptFinishHash(hHash, hash, sizeof(hash), 0) != 0)
	{
		cleanup();
		return "";
	}
	cleanup();

	static constexpr char hex[] = "0123456789ABCDEF";
	std::string result;
	result.reserve(64);
	for (const BYTE b : hash)
	{
		result += hex[b >> 4];
		result += hex[b & 0xF];
	}
	return result;
}

Binary Utils::GetBinaryResource(WORD id)
{
	Binary data = binary_resource_core(id);
	if (data.empty())
	{
		throw std::runtime_error("Failed to load resource");
	}
	return data;
}

bool Utils::IsElevated()
{
	return is_elevated_core();
}

void Utils::GetVersionNumbers(LPDWORD major, LPDWORD minor, LPDWORD build)
{
	using TRtlGetNtVersionNumbers = void (WINAPI)(LPDWORD, LPDWORD, LPDWORD);
	static TRtlGetNtVersionNumbers* fn = nullptr;
	if (fn == nullptr)
	{
		const auto hMod = GetModuleHandleA("ntdll.dll");
		if (hMod == nullptr) { return; }

		fn = reinterpret_cast<TRtlGetNtVersionNumbers*>(GetProcAddress(hMod, "RtlGetNtVersionNumbers"));
		if (fn == nullptr) { return; }
	}

	fn(major, minor, build);
	*build &= ~0xF0000000;
}

bool Utils::GetSetting(LPCSTR name, bool defaultValue)
{
	DWORD value = defaultValue ? 1 : 0;
	DWORD size = sizeof(value);

	const LSTATUS status = RegGetValueA(
		HKEY_CURRENT_USER,
		"Software\\Synapse",
		name,
		RRF_RT_REG_DWORD,
		nullptr,
		&value,
		&size
	);

	return status == ERROR_SUCCESS ? (value != 0) : defaultValue;
}

void Utils::SetSetting(LPCSTR name, bool value)
{
	HKEY hKey = nullptr;
	if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Synapse", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
	{
		const DWORD dwValue = value ? 1 : 0;
		RegSetValueExA(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwValue), sizeof(dwValue));
		RegCloseKey(hKey);
	}
}

DWORD Utils::GetSettingDWord(LPCSTR name, DWORD defaultValue)
{
	DWORD value = defaultValue;
	DWORD size = sizeof(value);

	const LSTATUS status = RegGetValueA(
		HKEY_CURRENT_USER,
		"Software\\Synapse",
		name,
		RRF_RT_REG_DWORD,
		nullptr,
		&value,
		&size
	);

	return status == ERROR_SUCCESS ? value : defaultValue;
}

void Utils::SetSettingDWord(LPCSTR name, DWORD value)
{
	set_setting_dword_core(name, value);
}

std::string Utils::GetStrSetting(LPCSTR name, const std::string& defaultValue)
{
	DWORD size = 0;
	const LSTATUS peek = RegGetValueA(
		HKEY_CURRENT_USER,
		"Software\\Synapse",
		name,
		RRF_RT_REG_SZ,
		nullptr,
		nullptr,
		&size
	);
	if (peek != ERROR_SUCCESS || size == 0) { return defaultValue; }

	std::vector<char> buffer(size);
	const LSTATUS status = RegGetValueA(
		HKEY_CURRENT_USER,
		"Software\\Synapse",
		name,
		RRF_RT_REG_SZ,
		nullptr,
		buffer.data(),
		&size
	);
	if (status != ERROR_SUCCESS || buffer.empty() || buffer.front() == '\0') { return defaultValue; }
	return buffer.data();
}

void Utils::SetStrSetting(LPCSTR name, const std::string& value)
{
	HKEY hKey = nullptr;
	if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Synapse", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
	{
		RegSetValueExA(hKey, name, 0, REG_SZ,
			reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>(value.size() + 1));
		RegCloseKey(hKey);
	}
}

void Utils::ShowConsole()
{
	AllocConsole();
	FILE* fDummy = nullptr;
	freopen_s(&fDummy, "CONIN$", "r", stdin);
	freopen_s(&fDummy, "CONOUT$", "w", stderr);
	freopen_s(&fDummy, "CONOUT$", "w", stdout);
}

void Utils::HideConsole()
{
	std::fclose(stdin);
	std::fclose(stderr);
	std::fclose(stdout);
	FreeConsole();
}
