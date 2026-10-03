#pragma once
#include <d3d11.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "core/types.h"
#include "app/assets.h"
#include "app/inject.h"
#include "LaunchInfo.h"

namespace app {

enum class AuthState : int { Login, Busy, Ready, Offline };

struct PanelSize {
    float width = 0.f;
    float height = 0.f;
};

constexpr float kTitleBarHeight = 32.f;

class App {
public:
    bool init(ID3D11Device* dev, const LaunchInfo& launchInfo);
    void shutdown();
    void frame(float dt);

    core::Rect panelRect() const { return m_panel; }
    void requestClose() { m_closing = true; }
    bool finished() const { return m_closing && m_closeT >= 1.f; }
    float fadeAlpha() const { return m_fadeIn * m_fade; }
    void autoStart() { m_inject.start(m_launch); }

private:
    void screenSplash(float alpha);
    void screenLogin(const core::Rect& panel, float alpha);
    void screenProduct(const core::Rect& panel, float alpha);
    void screenStatus(const core::Rect& panel, float alpha);

    void beginLogin();
    void loginWorker(const std::string& user, const std::string& key);
    // void goOffline();  // disabled - offline link removed from the login screen
    void launch();

    // Selected software on the product screen. 0 = Framework (the bundled
    // fallback), always listed; 1..N select the DLLs found next to the loader
    // (see Utils::EnumSidecarPayloads). Picking a row only selects it - launch
    // is triggered by the Secure Launch button.
    int m_selectedProduct = 0;

    // Payload DLLs enumerated once (lazily) from the loader's directory so the
    // product screen and the inject flow agree on indices (Framework is id 0).
    std::vector<std::wstring> m_payloads;
    bool m_payloadsInit = false;

    // Liquid background state: current colors ease toward the palette of the
    // selected software instead of snapping to it.
    core::Col m_bgA;
    core::Col m_bgB;
    float m_rainbowMix = 0.f;
    bool m_bgInit = false;

    Assets m_assets;
    InjectFlow m_inject;
    const LaunchInfo* m_launchInfo = nullptr;
    LaunchInfo m_launch;

    // Auth state: worker writes under the mutex, frame() reads once per frame.
    std::thread m_loginThread;
    std::mutex m_authMutex;
    AuthState m_auth = AuthState::Login;
    std::string m_authError;
    std::string m_user;
    std::string m_version;
    std::string m_changelog;
    std::string m_status;
    std::string m_expires;

    // Login form state (GUI thread only).
    std::string m_fieldUser;
    std::string m_fieldKey;

    // Inject stage used to trigger RPC updates on change.
    Stage m_lastStage = Stage::Idle;
    bool m_rpcBooted = false;

    // Music: Init.wav is kicked off the moment the login screen appears.
    bool m_musicStarted = false;

    PanelSize m_target;
    core::Rect m_panel;
    float m_curW = 0.f, m_curH = 0.f;
    float m_velW = 0.f, m_velH = 0.f;
    bool m_sizeInit = false;
    float m_boot = 0.f;
    bool m_bootDone = false;
    float m_bootBlend = 1.f;
    float m_statusBlend = 0.f;
    bool m_closing = false;
    float m_closeT = 0.f;
    float m_fade = 1.f;
    float m_fadeIn = 0.f;
    // Panel entrance scale: held at the splash size while loading, only ramps
    // to full size after loading finishes (in sync with the splash exit).
    float m_winT = 0.f;
    float m_openT = 0.f;
    float m_doneT = 0.f;
    bool m_doneClosing = false;
};

// Build stamp of THIS loader: shown as the "last updated" date, since the
// loader is always shipped bundled with the newest build.
const char* buildStamp();

// The exact launch status labels users see during injection.
const char* stageLabel(Stage s, const char* error);

}