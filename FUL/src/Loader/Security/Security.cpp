#include "Security.h"
#include "../../Utils/Log.h"
#include "../../Utils/xorstr.hpp"
#include "../../Utils/FULVM.h"

#include <Windows.h>
#include <intrin.h>
#include <iterator>
#include <winternl.h>

// All VM regions below are single straight-line blocks (one entry, one exit,
// `VM_END` immediately before the single return) and only call UNMARKED
// code. Each family protects one logical part.
namespace Security
{
	namespace
	{
		struct Patch
		{
			uint8_t* address = nullptr;
			uint8_t original = 0;
		};

		Patch g_patches[8];
		size_t g_patchCount = 0;

		void PatchByte(uint8_t* address, uint8_t value)
		{
			if (!address || g_patchCount >= std::size(g_patches)) { return; }

			DWORD oldProtect = 0;
			if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) { return; }

			g_patches[g_patchCount++] = { address, *address };
			*address = value;

			VirtualProtect(address, 1, oldProtect, &oldProtect);
			FlushInstructionCache(GetCurrentProcess(), address, 1);
		}

		// PUMA_WHITE: clears the classic PEB debugger markers (x64 offsets:
		// BeingDebugged at +0x02, NtGlobalFlag at +0xBC).
		static __declspec(noinline) void hardening_peb_core()
		{
			VM_PUMA_WHITE_START;
			const auto peb = reinterpret_cast<const uint8_t*>(__readgsqword(0x60));
			if (peb)
			{
				*const_cast<uint8_t*>(peb + 0x02) = 0;

				// Strip the three heap-validation bits from NtGlobalFlag.
				auto* globalFlags = reinterpret_cast<DWORD*>(const_cast<uint8_t*>(peb) + 0xBC);
				*globalFlags &= ~0x70u;
			}
			VM_PUMA_WHITE_END;
		}

		// PUMA_RED: unsets the heap debug flags the Windows manager derived
		// from the debugged NtGlobalFlag, so heap walking behaves normally.
		static __declspec(noinline) void hardening_heaps_core()
		{
			VM_PUMA_RED_START;
			const auto hNtdll = GetModuleHandleA(XorStr("ntdll.dll").c_str());
			if (hNtdll)
			{
				const auto fnRtlGetProcessHeaps = reinterpret_cast<ULONG(WINAPI*)(ULONG, PHANDLE)>(
					GetProcAddress(hNtdll, XorStr("RtlGetProcessHeaps").c_str()));
				if (fnRtlGetProcessHeaps)
				{
					// Default process heap count is small; the buffer is plenty.
					HANDLE heaps[64] = {};
					const ULONG count = fnRtlGetProcessHeaps(static_cast<ULONG>(std::size(heaps)), heaps);
					for (ULONG i = 0; i < count; i++)
					{
						if (!heaps[i]) { continue; }

						// x64 heap header: Flags at +0x40, ForceFlags at +0x44.
						// Clear the tail/free/parameter-checking bits the gflags
						// sets when the process is debugged.
						auto* flags = reinterpret_cast<DWORD*>(reinterpret_cast<uint8_t*>(heaps[i]) + 0x40);
						auto* forceFlags = reinterpret_cast<DWORD*>(reinterpret_cast<uint8_t*>(heaps[i]) + 0x44);
						*flags &= ~0x000000E0u;
						*forceFlags = 0;
					}
				}
			}
			VM_PUMA_RED_END;
		}

		// PUMA_BLACK: debuggers invoke these exports when attaching; let PatchByte
		// short-circuit them so the loader keeps running (no breakpoint honored).
		static __declspec(noinline) void hardening_breakpoints_core()
		{
			VM_PUMA_BLACK_START;
			const auto hNtdll = GetModuleHandleA(XorStr("ntdll.dll").c_str());
			if (hNtdll)
			{
				PatchByte(reinterpret_cast<uint8_t*>(GetProcAddress(hNtdll, XorStr("DbgUiRemoteBreakin").c_str())), 0xC3);
				PatchByte(reinterpret_cast<uint8_t*>(GetProcAddress(hNtdll, XorStr("DbgBreakPoint").c_str())), 0xC3);
			}
			VM_PUMA_BLACK_END;
		}

		// SHARK_WHITE: reads the same PEB markers without side effects.
		static __declspec(noinline) bool is_debugged_core()
		{
			VM_SHARK_WHITE_START;
			bool debugged = false;
			const auto peb = reinterpret_cast<const uint8_t*>(__readgsqword(0x60));
			if (peb)
			{
				const bool beingDebugged = *const_cast<uint8_t*>(peb + 0x02) != 0;
				const DWORD globalFlags = *reinterpret_cast<const DWORD*>(peb + 0xBC);
				debugged = beingDebugged || (globalFlags & 0x70) != 0;
			}
			VM_SHARK_WHITE_END;
			return debugged;
		}

		// SHARK_RED: unwinds the hardening patches in reverse order.
		static __declspec(noinline) void restore_core()
		{
			VM_SHARK_RED_START;
			for (size_t i = g_patchCount; i-- > 0;)
			{
				DWORD oldProtect = 0;
				if (VirtualProtect(g_patches[i].address, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
				{
					*g_patches[i].address = g_patches[i].original;
					VirtualProtect(g_patches[i].address, 1, oldProtect, &oldProtect);
					FlushInstructionCache(GetCurrentProcess(), g_patches[i].address, 1);
				}
			}
			g_patchCount = 0;
			VM_SHARK_RED_END;
		}
	}

	bool Init()
	{
		// Detect a debugger before wiping the markers it would otherwise leave.
		const bool wasDebugged = IsBeingDebugged();

		hardening_peb_core();
		hardening_heaps_core();
		hardening_breakpoints_core();

		if (wasDebugged) { Log::Warn("Security: debugger markers were present before hardening"); }
		Log::Info("Security: hardening active ({} patches)", g_patchCount);
		return true;
	}

	void Exit()
	{
		restore_core();
		Log::Info("Security: hardening removed");
	}

	bool IsBeingDebugged()
	{
		return is_debugged_core();
	}
}