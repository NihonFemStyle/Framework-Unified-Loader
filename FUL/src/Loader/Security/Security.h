#pragma once

// Anti-debug / hardening for the loader process only. Restored on Exit().
//
// NOTE: there is intentionally NO anti-VM detection here - Synapse supports
// cloud game streaming hosts (e.g. ShadowPC) that legitimately present
// themselves as virtual machines.
namespace Security
{
	// Installs PEB hardening and ntdll anti-attach patches. Idempotent.
	bool Init();

	// Restores every patched byte. Call on shutdown.
	void Exit();

	// Returns true while a debugger is attached (cheap PEB read).
	bool IsBeingDebugged();
}