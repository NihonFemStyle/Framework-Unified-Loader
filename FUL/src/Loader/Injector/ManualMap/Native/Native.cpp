#include "Native.h"

#include "../../../../Utils/Utils.h"

TRtlInsertInvertedFunctionTable* Native::GetRtlInsertInvertedFunctionTable()
{
	// RtlInsertInvertedFunctionTable is exported by ntdll.dll on 64-bit builds of Windows.
	// Note: newer Windows builds no longer export it; a null result is handled gracefully
	// by the shellcode (exception unwinding just won't be registered for the mapped module).
	const HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
	if (!hNtdll)
	{
		Log::Warn("Native: Failed to retrieve ntdll.dll");
		return nullptr;
	}

	const FARPROC fn = GetProcAddress(hNtdll, "RtlInsertInvertedFunctionTable");
	if (!fn)
	{
		Log::Warn("Native: Failed to retrieve RtlInsertInvertedFunctionTable");
		return nullptr;
	}

	Log::Debug("Native: RtlInsertInvertedFunctionTable @ {:p}", static_cast<void*>(fn));
	return reinterpret_cast<TRtlInsertInvertedFunctionTable*>(fn);
}