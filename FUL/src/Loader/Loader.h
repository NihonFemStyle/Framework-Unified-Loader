#pragma once
#include <functional>
#include "../LaunchInfo.h"

namespace Loader
{
	enum class Phase { WaitingForSteam, EnablingBypass, WaitingForGame, WaitingForClient, Loading, Done };
	using PhaseCallback = std::function<void(Phase)>;

	void Run(const LaunchInfo& launchInfo);
	bool Load(const LaunchInfo& launchInfo, const PhaseCallback& onPhase = {});
	bool Debug(const LaunchInfo& launchInfo, const PhaseCallback& onPhase = {});

	// Self updater
	bool CheckForUpdate(const LaunchInfo& launchInfo);
	void ApplyPendingUpdate();
}