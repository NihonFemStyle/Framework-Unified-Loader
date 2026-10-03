#pragma once
#include <string>

struct LaunchInfo
{
	bool Silent = false;   // -silent: mute the loader music playback
    bool Offline = false;  // -offline: skip login (requires a prior login)
    bool Secure = false;   // -secure: force the VACSAFE bypass even if already active
    bool NoBypass = false; // -nobypass: never run the VAC bypass (user has their own)
    bool UseLL = false;
    bool Debug = false;
    bool NoGH = false;
    bool Cache = false;

    std::wstring File;
    std::wstring URL;
    std::wstring GHPath;

    int Product = 0;     // index into the DLLs found next to the loader (first one by default)
};
