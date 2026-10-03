#pragma once
#include "../../Utils/Utils.h"

#include <string>

namespace Web
{
	struct Response
	{
		bool ok = false;
		DWORD status = 0;
		std::string body;
		std::wstring error;
	};

	// Generic HTTPS request through WinINet. `method` is "GET"/"POST"/...
	// `postBody` is sent raw for POST, `bearerToken` adds an Authorization
	// header when provided. Never blocks the UI thread.
	Response Request(
		const std::wstring& url,
		const std::wstring& method = L"GET",
		const std::string& postBody = {},
		const std::string& bearerToken = {});

	// Convenience: GET a URL and return the body (empty on failure).
	std::string DownloadString(const std::wstring& url);

	// Download a binary payload; when `bearerToken` is set the request carries
	// an Authorization: Bearer header (used for the private /api/dl endpoint).
	Binary DownloadFile(const std::wstring& url, const std::string& bearerToken = {});
}