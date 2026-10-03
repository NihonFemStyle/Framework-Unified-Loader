#include "Music.h"
#include "../../Utils/Log.h"
#include "../../Utils/Utils.h"
#include "../../../resource.h"

#include <Windows.h>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

// Single-track soundtrack: the bundled music.mp3 is extracted to
// %TEMP%\Synapse and looped through the MCI mpegvideo device from the login
// screen until the process exits. The old three-wav (Init/Launch/Inject)
// setup was replaced because multitrack playback caused driver issues.
namespace
{
	constexpr wchar_t kAlias[] = L"Synapse_music";

	bool g_ready = false;
	bool g_open = false;
	bool g_playing = false;

	// -silent: playback is suppressed but loop state stays coherent.
	bool g_muted = false;

	// True while the single loop should be kept alive by Update().
	bool g_keepLoop = false;

	std::wstring TempMusicPath()
	{
		WCHAR tempPath[MAX_PATH];
		if (GetTempPathW(MAX_PATH, tempPath) == 0) { return {}; }
		return std::wstring(tempPath) + L"Synapse\\music.mp3";
	}

	MCIERROR Mc(const std::wstring& cmd)
	{
		const MCIERROR err = mciSendStringW(cmd.c_str(), nullptr, 0, nullptr);
		if (err != 0)
		{
			wchar_t buf[256] = {};
			mciGetErrorStringW(err, buf, _countof(buf));
			Log::Warn(L"Music: '{}' failed (0x{:X}): {}", cmd, err, std::wstring(buf));
		}
		return err;
	}

	std::wstring Mode()
	{
		wchar_t buf[64] = {};
		mciSendStringW((std::wstring(L"status ") + kAlias + L" mode").c_str(), buf, _countof(buf), nullptr);
		return buf;
	}

	bool ExtractTrack()
	{
		const std::wstring path = TempMusicPath();
		if (path.empty()) { return false; }

		const std::filesystem::path dir = std::filesystem::path(path).parent_path();
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);

		Binary data;
		try { data = Utils::GetBinaryResource(IDR_MUSIC_MAIN); }
		catch (const std::exception& ex)
		{
			const std::wstring what(ex.what(), ex.what() + std::strlen(ex.what()));
			Log::Error(L"Music: failed to load bundled music.mp3: {}", what);
			return false;
		}
		if (data.empty())
		{
			Log::Error("Music: bundled music.mp3 is empty");
			return false;
		}

		if (!Utils::WriteBinaryFile(path, data))
		{
			Log::Error(L"Music: failed to write '{}'", path);
			return false;
		}

		Log::Info(L"Music: extracted '{}' ({} bytes)", path, data.size());
		return true;
	}

	void Open()
	{
		if (g_open) { return; }

		const std::wstring path = TempMusicPath();
		if (path.empty()) { return; }

		const std::wstring cmd = L"open \"" + path + L"\" type mpegvideo alias " + kAlias;
		const MCIERROR err = Mc(cmd);
		if (err != 0)
		{
			Log::Error("Music: failed to open music.mp3 for playback");
			return;
		}

		Mc(std::wstring(L"setaudio ") + kAlias + L" volume to 300");
		Log::Info(L"Music: opened '{}' ready for playback", path);
		g_open = true;
	}
}

namespace Music
{
	void Init()
	{
		if (g_ready) { return; }

		Log::Info("Music: initializing bundled sound track");

		ExtractTrack();
		Open();

		if (!g_open) { Log::Error("Music: music.mp3 is unavailable, the loader will be silent"); }
		g_ready = true;
	}

	void Play()
	{
		Init();
		if (!g_open) { Open(); }
		if (!g_open) { return; }

		if (!g_playing)
		{
			Mc(L"seek " + std::wstring(kAlias) + L" to start");
			if (!g_muted) { Mc(L"play " + std::wstring(kAlias)); }
			g_playing = true;
			g_keepLoop = true;
			Log::Info("Music: started the music loop");
		}
	}

	void PlayInit() { Play(); }
	void PlayLaunch() { Play(); }
	void PlayInject() { Play(); }

	void Update()
	{
		if (!g_ready || !g_keepLoop) { return; }

		// The mpegvideo driver does not accept a "repeat" flag here, so the
		// loop is manual: re-issue playback the moment the track reaches its end.
		if (_wcsicmp(Mode().c_str(), L"playing") != 0)
		{
			Mc(L"seek " + std::wstring(kAlias) + L" to start");
			if (!g_muted) { Mc(L"play " + std::wstring(kAlias)); }
		}
	}

	void SetMuted(bool muted)
	{
		if (g_muted == muted) { return; }
		g_muted = muted;
		Log::Info("Music: playback muted = {}", muted);
		if (muted)
		{
			Mc(L"stop " + std::wstring(kAlias));
			g_playing = false;
			g_keepLoop = false;
		}
	}

	void Shutdown()
	{
		Mc(L"close " + std::wstring(kAlias));
		g_open = false;
		g_playing = false;
		g_keepLoop = false;
		g_ready = false;
		Log::Info("Music: shutdown complete");
	}
}