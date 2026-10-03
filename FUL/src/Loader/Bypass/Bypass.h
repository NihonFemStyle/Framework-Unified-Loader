#pragma once

namespace Bypass
{
	// force = false (no args): only run the bypass when it isn't already active.
	// force = true  (-secure): restart Steam and re-inject regardless.
	void Run(bool force = false);

	// Relaunches TF2 through the still-alive bypassed Steam client. The game is
	// killed + relaunched fresh on retry attempts after a crash/hang, keeping
	// Steam and the VACSAFE module intact. No-op when the game is already up.
	bool LaunchGame();
}