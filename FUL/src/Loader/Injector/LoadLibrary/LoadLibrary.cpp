#include "LoadLibrary.h"
#include "../../../Utils/FULVM.h"

#include <filesystem>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Themida VM region. Single straight-line block, unmarked calls only.
// ---------------------------------------------------------------------------
namespace
{
	struct LlResult
	{
		LPVOID path = nullptr;
		HANDLE hThread = nullptr;
		bool wrote = false;
	};

	// LION_BLACK: allocates MAX_PATH in the target, writes the DLL path and
	// starts a remote LoadLibraryW call on it.
	static __declspec(noinline) LlResult ll_inject_core(HANDLE hTarget, const wchar_t* fullPath)
	{
		VM_LION_BLACK_START;
		LlResult result;
		const LPVOID lpPathAddress = VirtualAllocEx(hTarget, nullptr, MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (lpPathAddress)
		{
			result.path = lpPathAddress;
			if (WriteProcessMemory(hTarget, lpPathAddress, fullPath, MAX_PATH, nullptr))
			{
				result.wrote = true;
				result.hThread = CreateRemoteThread(hTarget, nullptr, NULL,
					reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadLibraryW), lpPathAddress, NULL, nullptr);
			}
		}
		VM_LION_BLACK_END;
		return result;
	}
}

bool LL::Inject(HANDLE hTarget, LPCWSTR fileName)
{
	// Get the full file path
	WCHAR fullPath[MAX_PATH];
	if (GetFullPathNameW(fileName, MAX_PATH, fullPath, nullptr) == 0)
	{
		throw std::runtime_error("Failed to retrieve full file path");
	}

	// Check if the file exists
	if (!std::filesystem::exists(fullPath))
	{
		throw std::invalid_argument("The given file does not exist");
	}

	// Allocate, copy and run the remote LoadLibraryW for the DLL path
	const LlResult result = ll_inject_core(hTarget, fullPath);
	if (!result.path)
	{
		throw std::runtime_error("Failed to allocate library path memory");
	}
	if (!result.wrote)
	{
		VirtualFreeEx(hTarget, result.path, 0, MEM_RELEASE);
		throw std::runtime_error("Failed to write library path");
	}
	if (!result.hThread)
	{
		VirtualFreeEx(hTarget, result.path, 0, MEM_RELEASE);
		throw std::runtime_error("Failed to create remote LoadLibrary thread");
	}

	Log::Debug("LL: Library path @ {:p}", result.path);

	// Wait for thread
	Log::Info("Waiting for target thread...");
	WaitForSingleObject(result.hThread, 15 * 1000);

	// Cleanup
	VirtualFreeEx(hTarget, result.path, 0, MEM_RELEASE);
	CloseHandle(result.hThread);
	CloseHandle(hTarget);

	Log::Info("LL: Done!");
	return true;
}
