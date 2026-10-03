#pragma once

#include <string>

// Minimal, dependency-free Discord Rich Presence client that talks to the local
// \\.\pipe\discord-ipc-* socket. Fully optional: every call degrades silently
// when Discord is not running. Mirrors the presence gimmick from the original
// loader references without pulling in the discord-rpc library.
namespace DiscordRpc
{
	// Starts the RPC client thread. No-op when already running.
	void Start(const std::string& clientId);

	// Queues a presence update. Safe to call from any thread and any rate; the
	// worker coalesces pending changes and only writes when they differ.
	void Update(
		const std::string& details,
		const std::string& state,
		const std::string& largeImage,
		const std::string& largeText,
		const std::string& smallImage = "",
		const std::string& smallText = "");

	// Stops the worker thread and closes the pipe.
	void Stop();
}