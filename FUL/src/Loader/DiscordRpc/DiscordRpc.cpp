#include "DiscordRpc.h"
#include "../../Utils/Log.h"
#include "../../Utils/xorstr.hpp"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace DiscordRpc
{
	namespace
	{
		struct Frame
		{
			uint32_t op = 0;
			uint32_t len = 0;
		};

		std::atomic<bool> g_stop{ true };
		std::thread g_thread;
		std::mutex g_mutex;
		std::string g_clientId;
		std::string g_details;
		std::string g_state;
		std::string g_largeImage;
		std::string g_largeText;
		std::string g_smallImage;
		std::string g_smallText;
		bool g_dirty = false;

		bool WriteFrame(HANDLE h, uint32_t op, const std::string& payload)
		{
			const Frame frame{ op, static_cast<uint32_t>(payload.size()) };
			DWORD written = 0;
			if (!WriteFile(h, &frame, sizeof(frame), &written, nullptr) || written != sizeof(frame)) { return false; }

			written = 0;
			return WriteFile(h, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)
				&& written == payload.size();
		}

		// Non-blocking drain of any inbound frames (handshake READY etc.). We never
		// block on the pipe so the worker can be stopped cleanly.
		void DrainFrames(HANDLE h)
		{
			for (int i = 0; i < 8; i++)
			{
				DWORD available = 0;
				if (!PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr) || available < sizeof(Frame)) { return; }

				Frame frame{};
				DWORD read = 0;
				if (!ReadFile(h, &frame, sizeof(frame), &read, nullptr) || read != sizeof(frame)) { return; }

				std::vector<char> payload(frame.len);
				read = 0;
				const DWORD avail = available > sizeof(Frame) ? available - sizeof(Frame) : 0;
				const DWORD len = static_cast<DWORD>(frame.len);
				const DWORD want = (std::min)(len, (std::min)(avail, static_cast<DWORD>(payload.size())));
				if (!ReadFile(h, payload.data(), want, &read, nullptr)) { return; }
			}
		}

		HANDLE TryOpenPipe()
		{
			for (int i = 0; i < 10; i++)
			{
				const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
				const HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
				if (h != INVALID_HANDLE_VALUE) { return h; }
			}
			return INVALID_HANDLE_VALUE;
		}

		std::string JsonEscape(const std::string& s)
		{
			std::string out;
			out.reserve(s.size() + 8);
			for (const char c : s)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
				{
					if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
					else { out += c; }
					break;
				}
				}
			}
			return out;
		}

		std::string BuildActivity()
		{
			std::string details, state, largeImage, largeText, smallImage, smallText;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				details = g_details;
				state = g_state;
				largeImage = g_largeImage;
				largeText = g_largeText;
				smallImage = g_smallImage;
				smallText = g_smallText;
			}

			std::string activity = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
				",\"activity\":{\"type\":0,\"details\":\"" + JsonEscape(details) + "\",\"state\":\"" + JsonEscape(state) +
				"\",\"assets\":{\"large_image\":\"" + JsonEscape(largeImage) + "\",\"large_text\":\"" + JsonEscape(largeText) +
				"\",\"small_image\":\"" + JsonEscape(smallImage) + "\",\"small_text\":\"" + JsonEscape(smallText) + "\"}}},\"nonce\":\"act\"}";
			return activity;
		}

		void Run()
		{
			while (!g_stop.load())
			{
				const HANDLE h = TryOpenPipe();
				if (h == INVALID_HANDLE_VALUE)
				{
					// Discord isn't running; retry every 5 seconds.
					for (int i = 0; i < 50 && !g_stop.load(); i++) { Sleep(100); }
					continue;
				}

				// Handshake (op 0).
				const std::string handshake = "{\"v\":1,\"client_id\":\"" + JsonEscape(g_clientId) + "\"}";
				if (!WriteFrame(h, 0, handshake))
				{
					CloseHandle(h);
					continue;
				}
				DrainFrames(h);

				// Subscribe so the activity is visible on our profile.
				WriteFrame(h, 2, "{\"cmd\":\"SUBSCRIBE\",\"evt\":\"ACTIVITY_JOIN\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) + "},\"nonce\":\"sub\"}");

				// Initial presence.
				if (!WriteFrame(h, 1, BuildActivity()))
				{
					CloseHandle(h);
					continue;
				}

				bool refreshed = false;
				LONGLONG lastBeat = GetTickCount64();
				while (!g_stop.load())
				{
					bool dirty = false;
					{
						std::lock_guard<std::mutex> lock(g_mutex);
						dirty = g_dirty;
						g_dirty = false;
					}

					if (dirty)
					{
						if (!WriteFrame(h, 1, BuildActivity())) { break; }
					}

					const LONGLONG now = GetTickCount64();
					if (now - lastBeat >= 15000 && !refreshed)
					{
						lastBeat = now;
						refreshed = true;
						if (!WriteFrame(h, 3, "{}")) { break; }
					}

					// Detect the pipe going away without blocking.
					DWORD available = 0;
					if (!PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr)) { break; }
					if (available >= sizeof(Frame))
					{
						DrainFrames(h);
						continue;
					}

					Sleep(250);
				}

				CloseHandle(h);
				DrainFrames(h); // no-op after close; kept symmetric
				g_dirty = true; // reconnect pushes the latest presence again
			}
		}
	}

	void Start(const std::string& clientId)
	{
		if (g_thread.joinable() || clientId.empty()) { return; }

		g_clientId = clientId;
		g_dirty = true;
		g_stop = false;
		g_thread = std::thread(Run);

		Log::Info("DiscordRpc: started in background");
	}

	void Update(const std::string& details, const std::string& state, const std::string& largeImage,
		const std::string& largeText, const std::string& smallImage, const std::string& smallText)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_details == details && g_state == state && g_largeImage == largeImage
			&& g_largeText == largeText && g_smallImage == smallImage && g_smallText == smallText) { return; }

		g_details = details;
		g_state = state;
		g_largeImage = largeImage;
		g_largeText = largeText;
		g_smallImage = smallImage;
		g_smallText = smallText;
		g_dirty = true;
	}

	void Stop()
	{
		g_stop = true;
		if (g_thread.joinable()) { g_thread.join(); }
		Log::Info("DiscordRpc: stopped");
	}
}