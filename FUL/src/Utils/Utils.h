#pragma once
#include <Windows.h>
#include "Log.h"

#include <string>
#include <vector>

using Binary = std::vector<BYTE>;

namespace Utils
{
	// Process utils
	DWORD FindProcess(LPCSTR procName);
	HANDLE GetProcessHandle(LPCSTR procName);

	// Wait for process
	DWORD WaitForProcess(LPCSTR procName, DWORD sTimeout = 10);
	HANDLE WaitForProcessHandle(LPCSTR procName, DWORD sTimeout = 10);
	bool WaitCloseProcess(LPCSTR procName, DWORD sTimeout = 10);
	bool WaitForModule(DWORD processId, LPCSTR moduleName, DWORD sTimeout = 10);

	// Binary utils
	Binary ReadBinaryFile(const std::wstring& fileName);
	bool WriteBinaryFile(const std::wstring& fileName, const Binary& data);
	Binary GetBinaryResource(WORD id);
	std::string ComputeSHA256(const Binary& data);

	// Exe-directory payload DLLs (every *.dll next to the loader except known
	// support libraries), sorted by name. Empty when no payloads are present.
	std::vector<std::wstring> EnumSidecarPayloads();

	// STL & WinApi utils
	bool IsElevated();
	void GetVersionNumbers(LPDWORD major, LPDWORD minor, LPDWORD build);

	// Settings (HKCU\Software\Synapse)
	bool GetSetting(LPCSTR name, bool defaultValue = false);
	void SetSetting(LPCSTR name, bool value);
	DWORD GetSettingDWord(LPCSTR name, DWORD defaultValue = 0);
	void SetSettingDWord(LPCSTR name, DWORD value);
	std::string GetStrSetting(LPCSTR name, const std::string& defaultValue = {});
	void SetStrSetting(LPCSTR name, const std::string& value);

	void ShowConsole();
	void HideConsole();
}
