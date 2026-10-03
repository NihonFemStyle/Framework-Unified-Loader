#include "Web.h"
#include "../../Utils/FULVM.h"
#include "../../Utils/xorstr.hpp"
#include <wininet.h>

#include <system_error>

namespace Web
{
	namespace
	{
		std::wstring ToWide(const std::string& s)
		{
			return std::wstring(s.begin(), s.end());
		}

		// Compile-time XOR'd so the UA never appears as plaintext in the binary.
		const std::wstring& UserAgent()
		{
			// ANSI XorStr + widen: the wide XorStrW template trips an MSVC ICE.
			static const std::string s = XorStr("Framework Unified Loader");
			static const std::wstring ua = ToWide(s);
			return ua;
		}

		// ------------------------------------------------------------------
		// Themida VM region (single straight-line block, unmarked calls only).
		// ------------------------------------------------------------------

		struct RequestCoreOut
		{
			std::wstring error;
			std::string body;
			DWORD status = 0;
		};

		// FISH_WHITE: the whole HTTP transaction as a single-exit region.
		static __declspec(noinline) void request_core(const std::wstring& url, const std::wstring& method, const std::string& postBody, const std::string& bearerToken, RequestCoreOut& out)
		{
			VM_FISH_WHITE_START;
			HINTERNET hInternet = nullptr;
			HINTERNET hRequest = nullptr;
			hInternet = InternetOpenW(UserAgent().c_str(), INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
			if (hInternet)
			{
				// 10-second timeouts so a slow panel never traps the user.
				DWORD timeout = 10000;
				InternetSetOptionW(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
				InternetSetOptionW(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
				InternetSetOptionW(hInternet, INTERNET_OPTION_SEND_TIMEOUT,    &timeout, sizeof(timeout));

				const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES;
				hRequest = InternetOpenUrlW(hInternet, url.c_str(), nullptr, 0, flags, 0);
				if (hRequest)
				{
					const bool isPost = _wcsicmp(method.c_str(), L"POST") == 0;

					std::wstring headers;
					if (isPost) { headers += L"Content-Type: application/json\r\n"; }
					if (!bearerToken.empty()) { headers += L"Authorization: Bearer " + ToWide(bearerToken) + L"\r\n"; }

					// Sends the request; for POST this carries the JSON body along.
					const char* bodyPtr = isPost ? postBody.data() : nullptr;
					const DWORD bodyLength = isPost ? static_cast<DWORD>(postBody.size()) : 0;
					if (!HttpSendRequestW(hRequest,
						headers.empty() ? nullptr : headers.c_str(),
						static_cast<DWORD>(headers.size()),
						const_cast<char*>(bodyPtr), bodyLength))
					{
						out.error = L"Request failed (0x" + std::to_wstring(GetLastError()) + L")";
					}
					else
					{
						DWORD status = 0;
						DWORD statusSize = sizeof(status);
						if (!HttpQueryInfoW(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusSize, nullptr))
						{
							status = 0;
						}
						out.status = status;

						char buffer[8192];
						DWORD bytesRead = 0;
						while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
						{
							out.body.append(buffer, bytesRead);
							bytesRead = 0;
						}
					}
				}
				else
				{
					out.error = L"Failed to open URL";
				}
			}
			else
			{
				out.error = L"Failed to initialize connection";
			}

			if (hRequest) { InternetCloseHandle(hRequest); }
			if (hInternet) { InternetCloseHandle(hInternet); }
			VM_FISH_WHITE_END;
		}
	}

	Response Request(const std::wstring& url, const std::wstring& method, const std::string& postBody, const std::string& bearerToken)
	{
		Response response;
		RequestCoreOut out;
		request_core(url, method, postBody, bearerToken, out);

		response.error = std::move(out.error);
		response.status = out.status;
		response.body = std::move(out.body);

		if (response.error.empty())
		{
			response.ok = response.status >= 200 && response.status < 300;
			if (!response.ok)
			{
				response.error = L"Server returned HTTP " + std::to_wstring(response.status);
			}
		}
		return response;
	}

	std::string DownloadString(const std::wstring& url)
	{
		const Response response = Request(url);
		return response.ok ? response.body : std::string();
	}

	Binary DownloadFile(const std::wstring& url, const std::string& bearerToken)
	{
		const Response response = Request(url, L"GET", {}, bearerToken);
		return response.ok ? Binary(response.body.begin(), response.body.end()) : Binary();
	}
}