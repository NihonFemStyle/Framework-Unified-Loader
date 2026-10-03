#include <windows.h>
#include <windowsx.h>
#include "../resource.h"
#include "LaunchInfo.h"
#include "Loader/Loader.h"
#include "Loader/Security/Security.h"
#include "Loader/DiscordRpc/DiscordRpc.h"
#include "Loader/Music/Music.h"
#include "Utils/Log.h"
#include "Utils/Utils.h"
#include "Utils/xorstr.hpp"

#include <VersionHelpers.h>
#include <iostream>
#include <thread>

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include "Gui/core/types.h"
#include "Gui/gfx/device.h"
#include "Gui/gfx/renderer.h"
#include "Gui/ui/ui.h"
#include "Gui/app/app.h"

using core::Rect;
using core::Vec2;

#define CHECK_ARG(szArg, bOut) if (_wcsicmp(arg, L##szArg) == 0) { (bOut) = true; continue; }
#define CHECK_ARG_STR(szArg, szOut) if (_wcsicmp(arg, L##szArg) == 0 && ++i < nArgs) { const auto nextArg = szArglist[i]; (szOut) = std::wstring(nextArg); continue; }

LaunchInfo GetLaunchInfo()
{
    int nArgs;
    LaunchInfo info{};
    bool cacheSet = false;
    LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (!szArglist) { return info; }
	
    for (int i = 0; i < nArgs; i++)
    {
        const auto arg = szArglist[i];

        if (_wcsicmp(arg, L"-cache") == 0) { info.Cache = true; cacheSet = true; continue; }
        if (_wcsicmp(arg, L"-no-cache") == 0) { info.Cache = false; cacheSet = true; continue; }

        CHECK_ARG("-silent", info.Silent)
        CHECK_ARG("-offline", info.Offline)
        CHECK_ARG("-secure", info.Secure)
        CHECK_ARG("-nobypass", info.NoBypass)
        CHECK_ARG("-ll", info.UseLL)
        CHECK_ARG("-debug", info.Debug)
        CHECK_ARG("-no-gh", info.NoGH)

        CHECK_ARG_STR("-file", info.File)
        CHECK_ARG_STR("-url", info.URL)
        CHECK_ARG_STR("-gh", info.GHPath)
    }

    LocalFree(szArglist);

    // Restore or persist the cache setting
    if (!cacheSet)
    {
        info.Cache = Utils::GetSetting("CacheBuild", false);
    }
    else
    {
        Utils::SetSetting("CacheBuild", info.Cache);
    }

    return info;
}

static const int kMargin = 26;
static const int kMaxPanelW = 520;
static const int kMaxPanelH = 336;

namespace
{
	// ANSI XorStr + widen: the wide XorStrW template trips an MSVC ICE.
	const wchar_t* WindowTitle()
	{
		static const std::string s = XorStr("Framework Unified Loader");
		static const std::wstring w(s.begin(), s.end());
		return w.c_str();
	}
}
static const float kBarHeight = app::kTitleBarHeight;

// Discord rich presence preset (see DiscordRpc.h).
static const char* kRpcAppId = "1171557443352928296";

struct AppState {
    gfx::Device device;
    gfx::Renderer renderer;
    app::App app;

    ui::Input input;
    bool prevDown = false;

    // Per-frame keyboard events collected by WndProc between frames.
    std::string pendingText;
    bool pendingBackspace = false;
    bool pendingEnter = false;
    bool pendingEscape = false;
    wchar_t pendingHighSurrogate = 0;
    bool haveHighSurrogate = false;

    bool dragging = false;
    POINT dragOrigin = {};
    RECT dragWindow = {};

    core::Rect panel;

    LARGE_INTEGER freq = {}, last = {};
    bool wantShot = false;
    std::wstring shotPath;
    float shotTime = 0.6f;
    float simTime = 0.f;
    bool running = true;
};

static AppState* gs = nullptr;

static void appendUtf8(std::string& out, wchar_t c) {
    if (c == 0) return;
    char buf[8] = {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, &c, 1, buf, (int)sizeof(buf), nullptr, nullptr);
    if (n > 0) out.append(buf, (size_t)n);
}

static void appendClipboardText(std::string& out) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return;
    if (!OpenClipboard(nullptr)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            out.append((std::wstring(p)).begin(), (std::wstring(p)).end());
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
}

static void handleChar(wchar_t c) {
    std::string& out = gs->pendingText;
    if (c == L'\r') {
        gs->pendingEnter = true;
        return;
    }
    if (c < 0x20 && c != L'\t') return;

    if (c >= 0xD800 && c <= 0xDBFF) {
        gs->pendingHighSurrogate = c;
        gs->haveHighSurrogate = true;
        return;
    }
    if (c >= 0xDC00 && c <= 0xDFFF) {
        if (gs->haveHighSurrogate) {
            wchar_t pair[2] = { gs->pendingHighSurrogate, c };
            char buf[8] = {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, pair, 2, buf, (int)sizeof(buf), nullptr, nullptr);
            if (n > 0) out.append(buf, (size_t)n);
        }
        gs->haveHighSurrogate = false;
        gs->pendingHighSurrogate = 0;
        return;
    }
    appendUtf8(out, c);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!gs) return DefWindowProc(hwnd, msg, wParam, lParam);
    switch (msg) {
        case WM_MOUSEMOVE:
            gs->input.mouse = Vec2((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            gs->input.down = true;
            const float mx = (float)GET_X_LPARAM(lParam);
            const float my = (float)GET_Y_LPARAM(lParam);
            const core::Rect& p = gs->panel;
            if (my > p.y && my < p.y + kBarHeight && mx > p.x && mx < p.r() - 40.f) {
                gs->dragging = true;
                GetCursorPos(&gs->dragOrigin);
                GetWindowRect(hwnd, &gs->dragWindow);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            ReleaseCapture();
            gs->input.down = false;
            gs->dragging = false;
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_BACK) gs->pendingBackspace = true;
            else if (wParam == VK_RETURN) gs->pendingEnter = true;
            else if (wParam == VK_ESCAPE) {
                // While a text field is focused the escape belongs to it
                // (defocuses instead of closing the window).
                if (ui::textFieldFocused()) gs->pendingEscape = true;
                else gs->app.requestClose();
            } else if (wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                appendClipboardText(gs->pendingText);
            }
            return 0;
        case WM_CHAR:
            handleChar((wchar_t)wParam);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            gs->running = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void applyDrag(HWND hwnd) {
    if (!gs->dragging) return;
    POINT now;
    GetCursorPos(&now);
    SetWindowPos(hwnd, nullptr, gs->dragWindow.left + (now.x - gs->dragOrigin.x),
                 gs->dragWindow.top + (now.y - gs->dragOrigin.y), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdline, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    LaunchInfo launchInfo = GetLaunchInfo();
    if (launchInfo.Debug)
    {
        Utils::ShowConsole();
        Log::EnableFileLogging(true);
        Log::SetLevel(LogLevel::Debug);
        Log::Info("Framework build: {}", __TIMESTAMP__);
    }

    // Apparently people are still using Windows 7 in 2023...
    if (!IsWindows8OrGreater())
    {
        MessageBoxA(nullptr, "Your Windows version is no longer supported!\nPlease upgrade to Windows 8 or above.", "Outdated OS", MB_OK | MB_ICONWARNING);
        return EXIT_FAILURE;
    }

    // Check privileges
    if (!Utils::IsElevated())
    {
        MessageBoxA(nullptr, XorStr("Please restart Framework Unified Loader as administrator!").c_str(), XorStr("Missing elevation").c_str(), MB_OK | MB_ICONWARNING);
        return EXIT_FAILURE;
    }

    // Warn about non-default launch arguments
    if (launchInfo.NoBypass)
        MessageBoxA(nullptr,
                    "Running without the VACSAFE module (-nobypass).\n"
                    "This is insecure - only use it if you have your own external VAC bypass.",
                    "VACSAFE Module Disabled", MB_OK | MB_ICONWARNING);
    if (launchInfo.NoGH)
        MessageBoxA(nullptr, "Legacy Injector selected, Framework may not load successfully", "Legacy Injector", MB_OK | MB_ICONWARNING);
    if (!launchInfo.File.empty())
        MessageBoxA(nullptr, "Custom Module Set, This is for development use at your own risk", "Custom Module", MB_OK | MB_ICONWARNING);
    if (!launchInfo.URL.empty())
        MessageBoxA(nullptr, "Custom URL Set, This is for development use at your own risk", "Custom URL", MB_OK | MB_ICONWARNING);

    // -secure and -nobypass are mutually exclusive bypass modes.
    if (launchInfo.Secure && launchInfo.NoBypass)
    {
        MessageBoxA(nullptr, "-secure and -nobypass cannot be used together.\nChoose one VACSAFE mode.",
                    "Conflicting arguments", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }

    // Anti-debug / PEB hardening. No anti-VM (the loader must run on Shadow).
    Security::Init();
    Log::Info("Loader: security initialized");

    AppState state;
    gs = &state;

    const std::wstring cl = cmdline ? cmdline : L"";
    bool headless = false;

    auto argAfter = [&](const wchar_t* flag, std::wstring& out) -> bool {
        size_t p = cl.find(flag);
        if (p == std::wstring::npos) return false;
        std::wstring rest = cl.substr(p + wcslen(flag));
        size_t s = rest.find_first_not_of(L" ");
        if (s == std::wstring::npos) return false;
        std::wstring value = rest.substr(s);
        size_t e = value.find(L' ');
        if (e != std::wstring::npos) value = value.substr(0, e);
        if (value.empty() || value[0] == L'-') return false;
        out = value;
        return true;
    };

    if (cl.find(L"-shot") != std::wstring::npos) {
        state.wantShot = true;
        headless = true;
        state.shotPath = L"shot.bmp";
        std::wstring value;
        if (argAfter(L"-shot", value)) state.shotPath = value;
        if (argAfter(L"-t", value)) state.shotTime = (float)_wtof(value.c_str());
    }
    // -inject auto-launches on startup. Like -offline it skips the login screen
    // (falls back to login if the user has never logged in before).
    const bool autoInject = cl.find(L"-inject") != std::wstring::npos;
    if (autoInject) launchInfo.Offline = true;
    const bool forceHover = cl.find(L"-hover") != std::wstring::npos;

    // Background self-update check (skipped in headless capture mode)
    std::thread updaterThread;
    if (!launchInfo.Cache && !headless)
    {
        const LaunchInfo checkInfo = launchInfo;
        updaterThread = std::thread([checkInfo]() {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            try { Loader::CheckForUpdate(checkInfo); }
            catch (const std::exception& ex) { Log::Error("Updater: {}", ex.what()); }
            catch (...) { Log::Error("Updater: unknown error"); }
            CoUninitialize();
        });
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDR_ICON));
    wc.hIconSm = LoadIcon(hInst, MAKEINTRESOURCE(IDR_ICON));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"FrameworkUnifiedLoader";
    RegisterClassExW(&wc);

    const int winW = kMaxPanelW + kMargin * 2;
    const int winH = kMaxPanelH + kMargin * 2;
    const int sx = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    const int sy = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW | WS_EX_TOPMOST, wc.lpszClassName, WindowTitle(),
                                WS_POPUP, sx, sy, winW, winH, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    Log::Info("Window: created ({}x{}) at ({}, {})", winW, winH, sx, sy);

    if (!state.device.create(hwnd, winW, winH)) return 1;
    if (!state.renderer.create(state.device.dev())) return 1;
    if (!state.app.init(state.device.dev(), launchInfo)) return 1;
    Log::Info("Loader: window, D3D device and app ready");

    if (!headless) {
        DiscordRpc::Start(kRpcAppId);
        DiscordRpc::Update(XorStr("Preparing to start").str(), XorStr("TF2 Unified Software").str(), XorStr("framework").str(), XorStr("Framework Unified Loader").str());
    }

    if (!headless) {
        ShowWindow(hwnd, SW_SHOW);
        // Re-assert the always-on-top state: some compositors drop it around
        // ShowWindow, and TF2 itself may try to regain focus. Bringing the
        // loader back to the top keeps it visible over the game overlay.
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (autoInject && !headless) state.app.autoStart();

    QueryPerformanceFrequency(&state.freq);
    QueryPerformanceCounter(&state.last);
    timeBeginPeriod(1);

    while (state.running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) state.running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!state.running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - state.last.QuadPart) / (float)state.freq.QuadPart;
        state.last = now;
        dt = (std::min)((std::max)(dt, 1.f / 240.f), 0.05f);
        if (state.wantShot) dt = 1.f / 60.f;
        state.simTime += dt;

        applyDrag(hwnd);

        ui::Input frameInput = state.input;
        if (forceHover)
            frameInput.mouse = Vec2(state.panel.center().x, state.panel.y + kBarHeight + 32.f);
        frameInput.pressed = state.input.down && !state.prevDown;
        frameInput.released = !state.input.down && state.prevDown;
        state.prevDown = state.input.down;

        // Fold per-frame keyboard events into the input snapshot.
        frameInput.text = std::move(state.pendingText);
        state.pendingText.clear();
        frameInput.backspace = state.pendingBackspace;
        frameInput.enter = state.pendingEnter;
        frameInput.escape = state.pendingEscape;
        state.pendingBackspace = false;
        state.pendingEnter = false;
        state.pendingEscape = false;

        ui::newFrame(frameInput, dt, (float)winW, (float)winH);
        state.app.frame(dt);
        ui::endFrame();
        state.panel = state.app.panelRect();

        state.device.beginFrame();
        state.renderer.render(state.device.ctx(), ui::dl(), winW, winH, state.app.fadeAlpha(), ui::g().time);
        state.device.present(true);

        if (state.app.finished()) state.running = false;

        if (state.wantShot && state.simTime >= state.shotTime) {
            Log::Info("Loader: capturing shot at sim {:.2f}s", state.simTime);
            if (!state.device.captureBackbuffer(state.shotPath))
                Log::Warn(L"Loader: captureBackbuffer failed for '{}'", state.shotPath);
            state.running = false;
        }

        if (!state.wantShot) {
            LARGE_INTEGER after;
            QueryPerformanceCounter(&after);
            const double elapsed = (double)(after.QuadPart - state.last.QuadPart) / (double)state.freq.QuadPart;
            const double wait = 1.0 / 60.0 - elapsed;
            if (wait > 0.0005) Sleep((DWORD)(wait * 1000.0));
        }
    }

    timeEndPeriod(1);

    Log::Info("Loader: main loop exited, shutting down");
    state.app.shutdown();
    Security::Exit();
    DiscordRpc::Stop();
    Music::Shutdown();
    state.renderer.destroy();
    state.device.destroy();
    DestroyWindow(hwnd);

    if (updaterThread.joinable()) updaterThread.join();
    Loader::ApplyPendingUpdate();

    // Hide debug console
    if (launchInfo.Debug) { Utils::HideConsole(); }
    CoUninitialize();
    return 0;
}