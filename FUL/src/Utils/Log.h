#pragma once
#include <Windows.h>
#include <format>
#include <fstream>
#include <iostream>
#include <chrono>
#include <filesystem>

enum class LogLevel
{
	Debug = 0,
	Info,
	Warn,
	Error,
	None
};

class Log
{
	inline static LogLevel Level = LogLevel::Debug;
	inline static std::ofstream File;

	// File logging is opt-in: the loader only writes Synapse.log when it is
	// started with -debug. Without it everything stays in-memory/console.
	inline static bool FileEnabled = false;

	static std::string GetTimeStamp()
	{
		const auto now = std::chrono::system_clock::now();
		return std::format("{:%T}", now);
	}

	static std::wstring GetTimeStampW()
	{
		const auto now = std::chrono::system_clock::now();
		return std::format(L"{:%T}", now);
	}

	static std::ofstream& GetFile()
	{
		if (!FileEnabled) { return File; }

		// Open the log file next to the executable once
		if (!File.is_open())
		{
			WCHAR path[MAX_PATH];
			if (GetModuleFileNameW(nullptr, path, MAX_PATH) != 0)
			{
				std::filesystem::path exePath(path);
				exePath.replace_extension(L".log");
				File.open(exePath, std::ios::app);
			}
		}

		return File;
	}

	template<typename... Args>
	static void LogPrefix(const std::string& prefix, std::format_string<Args...> fmt, Args&&... args)
	{
		const auto line = std::format("{} [{}] {}\n", GetTimeStamp(), prefix, std::format(fmt, std::forward<Args>(args)...));
		std::cout << line;
		if (GetFile().is_open())
		{
			GetFile() << line;
			GetFile().flush();
		}
	}

	template<typename... Args>
	static void LogPrefixW(const std::wstring& prefix, std::wformat_string<Args...> fmt, Args&&... args)
	{
		const auto line = std::format(L"{} [{}] {}\n", GetTimeStampW(), prefix, std::format(fmt, std::forward<Args>(args)...));
		std::wcout << line;
		if (GetFile().is_open())
		{
			GetFile() << std::filesystem::path(line).string();
			GetFile().flush();
		}
	}

public:
	static void SetLevel(LogLevel level)
	{
		Level = level;
	}

	// Enables/disables writing to Synapse.log (only when run with -debug).
	static void EnableFileLogging(bool enabled)
	{
		FileEnabled = enabled;
	}

	template<typename... Args>
	static void Debug(const std::format_string<Args...> fmt, Args&&... args)
	{
		if (Level > LogLevel::Debug) { return; }
		LogPrefix("Debug", fmt, std::forward<Args>(args)...);
	}

	template<typename... Args>
	static void Debug(const std::wformat_string<Args...> fmt, Args&&... args)
	{
		if (Level > LogLevel::Debug) { return; }
		LogPrefixW(L"Debug", fmt, std::forward<Args>(args)...);
	}

	template<typename... Args>
	static void Info(const std::format_string<Args...> fmt, Args&&... args)
	{
		if (Level > LogLevel::Info) { return; }
		LogPrefix("Info", fmt, std::forward<Args>(args)...);
	}

	template<typename... Args>
	static void Info(const std::wformat_string<Args...> fmt, Args&&... args)
	{
		if (Level > LogLevel::Info) { return; }
		LogPrefixW(L"Info", fmt, std::forward<Args>(args)...);
	}

	template<typename... Args>
	static void Warn(const std::format_string<Args...> fmt, Args&&... args)
	{
		if (Level > LogLevel::Warn) { return; }
		LogPrefix("Warn", fmt, std::forward<Args>(args)...);
	}

	template<typename... Args>
	static void Warn(const std::wformat_string<Args...> fmt, Args&&... args)
	{
		if (Level > LogLevel::Warn) { return; }
		LogPrefixW(L"Warn", fmt, std::forward<Args>(args)...);
	}

	template<typename... Args>
	static void Error(const std::format_string<Args...> fmt, Args&&... args)
	{
		if (Level > LogLevel::Error) { return; }
		LogPrefix("Error", fmt, std::forward<Args>(args)...);
	}

	template<typename... Args>
	static void Error(const std::wformat_string<Args...> fmt, Args&&... args)
	{
		if (Level > LogLevel::Error) { return; }
		LogPrefixW(L"Error", fmt, std::forward<Args>(args)...);
	}
};
