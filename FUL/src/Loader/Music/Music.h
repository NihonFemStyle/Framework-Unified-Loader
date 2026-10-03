#pragma once

// The loader soundtrack is a single bundled music.mp3 that starts when the
// login screen is shown and plays on a loop until the process exits. The old
// three-track (Init/Launch/Inject) setup was dropped because multitrack
// playback caused MCI driver issues; there is no per-screen music anymore,
// so every phase transition reuses the same looping track.
namespace Music
{
	// Extracts the bundled music.mp3 into %TEMP%\Synapse (call once at startup).
	void Init();

	// Starts the loop if the track is not already playing. These three exist so
	// the existing stage-transition call sites work, but they all behave the
	// same: the same track keeps playing until the loader closes.
	void PlayInit();
	void PlayLaunch();
	void PlayInject();

	// -silent: disables playback entirely while keeping loop state coherent.
	void SetMuted(bool muted);

	// Keeps the loop alive. Call once per frame: this driver does not accept a
	// "repeat" flag, so the play command is re-issued here when the track ends.
	void Update();

	// Closes the MCI device. Safe to call with nothing playing.
	void Shutdown();
}