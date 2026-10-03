#pragma once
#include <atomic>
#include <thread>
#include "LaunchInfo.h"

namespace app {

enum class Stage { Idle, WaitingForSteam, EnablingBypass, WaitingForGame, WaitingForClient, Loading, Done, Failed };

class InjectFlow {
public:
    void start(const LaunchInfo& info);
    void shutdown();
    void update(float dt);

    Stage stage() const { return m_stage.load(std::memory_order_acquire); }
    bool active() const { return stage() != Stage::Idle; }
    bool running() const;
    bool done() const { return stage() == Stage::Done; }
    bool failed() const { return stage() == Stage::Failed; }
    const char* errorText() const { return m_error; }

private:
    void setStage(Stage s) { m_stage.store(s, std::memory_order_release); }

    std::atomic<Stage> m_stage{Stage::Idle};
    char m_error[256]{};
    std::thread m_thread;
};

}