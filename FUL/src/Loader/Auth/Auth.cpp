#include "Auth.h"
#include "creds.h"
#include "../Web/Web.h"
#include "../../Utils/Utils.h"
#include "../../Utils/xorstr.hpp"
#include "../../Utils/FULVM.h"

#include <Windows.h>
#include <cstdio>
#include <vector>

// The salt is intentionally public (it ships in the panel tooling) - its job
// is to force a recompute of every stored hash when rotated and to defeat
// rainbow tables. The actual protection is that keys stay secret and are only
// ever stored as hashes.
namespace
{
	const std::string& Salt()
	{
		static const std::string salt = XorStr("Synapse:k:7f3d9c2a:1b4e:4f8a:9c2d:e5f1a2b3c4d5");
		return salt;
	}

	// Baked credentials, XOR'd so they never appear as plaintext in the binary.
	const std::string& BakedUsername()
	{
		static const std::string user = XorStr(SYNAPSE_CRED_USERNAME).str();
		return user;
	}

	const std::string& BakedKeyHash()
	{
		static const std::string hash = XorStr(SYNAPSE_CRED_KEY_HASH).str();
		return hash;
	}

	const std::string& BakedExpires()
	{
		static const std::string expiry = XorStr(SYNAPSE_CRED_EXPIRES).str();
		return expiry;
	}

	// The "unknown" fallback for the build stamp, XOR'd so it never lands
	// as plaintext alongside the version_core VM region.
	const std::string& UnknownStamp()
	{
		static const std::string stamp = XorStr("unknown").str();
		return stamp;
	}
}

namespace Auth
{
	namespace
	{
		std::wstring ToWide(const std::string& s)
		{
			return std::wstring(s.begin(), s.end());
		}

		std::string Lower(const std::string& s)
		{
			std::string out = s;
			for (char& c : out)
			{
				if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
			}
			return out;
		}

		// Uppercase-hex SHA-256 of (key + salt), same alphabet as admin.html.
		// Deliberately UNMARKED: it is called from the VM'd login cores, and a
		// marked function may only call unmarked code (no nested VM markers).
		static std::string HashKey(const std::string& key)
		{
			const std::string data = key + Salt();
			const Binary bytes(data.begin(), data.end());
			return Utils::ComputeSHA256(bytes);
		}

		// Minimal single-field JSON extraction: "key":"value".
		std::string json_str_core(const std::string& json, const std::string& key, size_t from);
		std::string JsonStr(const std::string& json, const std::string& key, size_t from)
		{
			return json_str_core(json, key, from);
		}

		// Looks up `user` inside the users array of auth.json. Returns true and
		// fills recordKey/recordExpires when a matching entry is found.
		bool FindUser(const std::string& json, const std::string& user, std::string& recordKey, std::string& recordExpires)
		{
			const std::string needle = "\"user\":\"";
			std::string lowerUser = Lower(user);
			size_t cursor = 0;

			for (;;)
			{
				const size_t hit = json.find(needle, cursor);
				if (hit == std::string::npos) { return false; }

				const size_t begin = hit + needle.size();
				const size_t end = json.find('"', begin);
				if (end == std::string::npos) { return false; }

				if (Lower(json.substr(begin, end - begin)) == lowerUser)
				{
					const size_t objEnd = json.find('}', end);
					recordKey = JsonStr(json, "key", end);
					recordExpires = JsonStr(json, "expires", end);
					(void)objEnd;
					return true;
				}

				cursor = end + 1;
			}
		}

		// true when an expires field ("Never" or "YYYY-MM-DD") is still valid.
		bool NotExpired(const std::string& expires)
		{
			if (Lower(expires) == "never" || expires.empty()) { return true; }

			SYSTEMTIME st{};
			GetLocalTime(&st);
			char today[16];
			std::snprintf(today, sizeof(today), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
			return expires >= today;
		}

		// ------------------------------------------------------------------
		// Themida VM regions. Every core below is a single straight-line block
		// (one entry, one exit, `VM_END` immediately before the return) and may
		// only call UNMARKED helpers (HashKey/Lower/NotExpired/JsonStr). Each
		// family corresponds to one logical part, one family per region.
		// ------------------------------------------------------------------

		// TIGER_WHITE: baked username comparison.
		static __declspec(noinline) bool login_baked_user_core(const std::string& user)
		{
			VM_TIGER_WHITE_START;
			const bool userOk = Lower(user) == Lower(BakedUsername());
			VM_TIGER_WHITE_END;
			return userOk;
		}

		// TIGER_RED: baked salted-key hash comparison plus expiry.
		static __declspec(noinline) short login_baked_key_core(const std::string& key)
		{
			VM_TIGER_RED_START;
			short result = 0; // 0 = bad key, 1 = expired, 2 = good
			if (Lower(HashKey(key)) == Lower(BakedKeyHash()))
			{
				if (!NotExpired(BakedExpires())) { result = 1; }
				else { result = 2; }
			}
			VM_TIGER_RED_END;
			return result;
		}

		// Remote-key verification against an auth.json record. UNMARKED by
		// design: this path only exists when creds.h is empty (local/dev
		// builds), never in the processed, shipped binary.
		static short login_remote_core(const std::string& key, const std::string& recordKey, const std::string& recordExpires)
		{
			short verdict = 0; // 0 = denied, 1 = expired, 2 = granted
			if (Lower(HashKey(key)) == Lower(recordKey))
			{
				if (!NotExpired(recordExpires)) { verdict = 1; }
				else { verdict = 2; }
			}
			return verdict;
		}

		// TIGER_BLACK: PE TimeDateStamp of the running loader.
		static __declspec(noinline) std::string version_core()
		{
			VM_TIGER_BLACK_START;
			std::string stamp = UnknownStamp();
			const auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
			if (base)
			{
				const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
				if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE)
				{
					const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
					if (nt && nt->Signature == IMAGE_NT_SIGNATURE)
					{
						stamp = std::to_string(nt->FileHeader.TimeDateStamp);
					}
				}
			}
			VM_TIGER_BLACK_END;
			return stamp;
		}

		// FISH_RED: single-field JSON extraction "key":"value".
		static __declspec(noinline) std::string json_str_core(const std::string& json, const std::string& key, size_t from)
		{
			VM_FISH_RED_START;
			std::string out;
			const std::string needle = "\"" + key + "\":\"";
			const size_t start = json.find(needle, from);
			if (start != std::string::npos)
			{
				const size_t begin = start + needle.size();
				const size_t end = json.find('"', begin);
				if (end != std::string::npos && end != begin) { out = json.substr(begin, end - begin); }
			}
			VM_FISH_RED_END;
			return out;
		}
	}

	const std::string& BaseUrl()
	{
		static const std::string url = XorStr("https://yoursite.here");
		return url;
	}

	const std::string& LoaderExeUrl()
	{
		static const std::string url = XorStr("https://yoursite.here/loader.exe");
		return url;
	}

	const std::string& ClientVersion()
	{
		static const std::string version = []() -> std::string
		{
			return std::string(XorStr("3.1.1.")) + version_core();
		}();
		return version;
	}

	const std::string& BuildId()
	{
		static const std::string id = XorStr(SYNAPSE_CRED_BUILD);
		return id;
	}

	void FetchMeta(std::string& version, std::string& changelog, std::string& status)
	{
		const std::string json = Web::DownloadString(ToWide(BaseUrl()) + L"/config.json");
		version = JsonStr(json, "version", 0);
		changelog = JsonStr(json, "changelog", 0);
		status = JsonStr(json, "status", 0);
	}

	Session Login(const std::string& user, const std::string& key)
	{
		Session session;

		if (user.empty() || key.empty())
		{
			session.error = "Enter a username and key.";
			return session;
		}

		// Baked build (creds.h filled in by the release workflow): the expected
		// username, salted key hash and expiry are compiled in, so the check is
		// entirely local — no auth.json is fetched for these builds.
		if (!BakedUsername().empty())
		{
			const bool userOk = login_baked_user_core(user);
			const short keyResult = login_baked_key_core(key);
			const bool expired = keyResult == 1;
			const bool grants = userOk && keyResult == 2;

			if (!grants)
			{
				Sleep(150);
				session.error = expired ? "This access key has expired." : "Access denied.";
				return session;
			}

			session.ok = true;
			session.user = BakedUsername();
			session.expires = BakedExpires();
			session.buildId = BuildId();
			Log::Info("Auth: baked access for {} (build {})", session.user,
					  session.buildId.empty() ? "<none>" : session.buildId);
			FetchMeta(session.version, session.changelog, session.status);
			return session;
		}

		const std::string authJson = Web::DownloadString(ToWide(BaseUrl()) + L"/auth.json");
		if (authJson.empty())
		{
			session.error = "Panel is unreachable, using offline build.";
			return session;
		}

		std::string recordKey;
		std::string recordExpires;
		if (!FindUser(authJson, user, recordKey, recordExpires))
		{
			// Burn identical time on failure to make enumeration harder.
			Sleep(150);
			session.error = "Access denied.";
			return session;
		}

		const short verdict = login_remote_core(key, recordKey, recordExpires);
		if (verdict == 0)
		{
			Sleep(150);
			session.error = "Access denied.";
			return session;
		}
		if (verdict == 1)
		{
			session.error = "This access key has expired.";
			return session;
		}

		session.ok = true;
		session.user = user;
		session.expires = recordExpires.empty() ? XorStr("Never").str() : recordExpires;

		FetchMeta(session.version, session.changelog, session.status);
		return session;
	}
}