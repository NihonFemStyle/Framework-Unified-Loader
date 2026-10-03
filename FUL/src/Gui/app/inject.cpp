#include "app/inject.h"
#include "Loader/Loader.h"
#include "Utils/Log.h"

#include <combaseapi.h>
#include <cstring>

namespace app {

static const char* PhaseName(Loader::Phase phase) {
    switch (phase) {
    case Loader::Phase::WaitingForSteam: return "WaitingForSteam";
    case Loader::Phase::EnablingBypass: return "EnablingBypass";
    case Loader::Phase::WaitingForGame: return "WaitingForGame";
    case Loader::Phase::WaitingForClient: return "WaitingForClient";
    case Loader::Phase::Loading: return "Loading";
    case Loader::Phase::Done: return "Done";
    }
    return "Unknown";
}

void InjectFlow::start(const LaunchInfo& info) {
    if (m_thread.joinable()) return;

    Log::Info("Inject: flow started (useLL = {}, secure = {}, nobypass = {}, silent = {})",
              info.UseLL, info.Secure, info.NoBypass, info.Silent);

    m_thread = std::thread([this, info]() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        const auto onPhase = [this](Loader::Phase phase) {
            Log::Info("Inject: phase -> {}", PhaseName(phase));
            switch (phase) {
            case Loader::Phase::WaitingForSteam: setStage(Stage::WaitingForSteam); break;
            case Loader::Phase::EnablingBypass: setStage(Stage::EnablingBypass); break;
            case Loader::Phase::WaitingForGame: setStage(Stage::WaitingForGame); break;
            case Loader::Phase::WaitingForClient: setStage(Stage::WaitingForClient); break;
            case Loader::Phase::Loading: setStage(Stage::Loading); break;
            case Loader::Phase::Done: setStage(Stage::Done); break;
            }
        };

        try {
            const bool ok = info.UseLL
                ? Loader::Debug(info, onPhase)
                : Loader::Load(info, onPhase);
            Log::Info("Inject: flow finished (ok = {})", ok);
        } catch (const std::exception& ex) {
            Log::Error("Inject: flow failed: {}", ex.what());
            strncpy_s(m_error, sizeof(m_error), ex.what(), _TRUNCATE);
            setStage(Stage::Failed);
        } catch (...) {
            Log::Error("Inject: flow failed with an unknown exception");
            strncpy_s(m_error, sizeof(m_error), "An unexpected error occurred", _TRUNCATE);
            setStage(Stage::Failed);
        }

        CoUninitialize();
    });
}

void InjectFlow::shutdown() {
    if (m_thread.joinable()) m_thread.join();
}

bool InjectFlow::running() const {
    const Stage s = stage();
    return s != Stage::Idle && s != Stage::Done && s != Stage::Failed;
}

void InjectFlow::update(float dt) {
    (void)dt;
}

}