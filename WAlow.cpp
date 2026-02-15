// WAlow.cpp : Multi-app social media browser
// Tabs: WhatsApp, Telegram, Instagram, TikTok, LinkedIn, Facebook + custom
// Inactive tabs hidden from toolbar. Zoom applies to browser content.
// Minimizes to tray. Optional run-at-startup. Folder browse for dump path.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <wrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstdio>

#include <d3d11.h>
#include <dxgi.h>

#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#include "WAlow.h"

using Microsoft::WRL::ComPtr;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static AppState g_app;
static const int TOOLBAR_HEIGHT = 34;
static const int STATUSBAR_HEIGHT = 20;

// ============================================================================
// AppState helpers
// ============================================================================

int AppState::GetTotalTabCount() const { return BUILTIN_COUNT + settings.customTabCount; }

const char* AppState::GetTabName(int i) const
{
    if (i < BUILTIN_COUNT) return g_builtinApps[i].name;
    int ci = i - BUILTIN_COUNT;
    if (ci >= 0 && ci < settings.customTabCount) return settings.customTabs[ci].name;
    return "?";
}

std::wstring AppState::GetTabUrl(int i) const
{
    if (i < BUILTIN_COUNT) return g_builtinApps[i].url;
    int ci = i - BUILTIN_COUNT;
    if (ci >= 0 && ci < settings.customTabCount)
    {
        const char* u = settings.customTabs[ci].url;
        int len = MultiByteToWideChar(CP_UTF8, 0, u, -1, nullptr, 0);
        std::wstring w(len - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, u, -1, w.data(), len);
        return w;
    }
    return L"about:blank";
}

bool AppState::IsTabEnabled(int i) const
{
    if (i < 0 || i >= GetTotalTabCount()) return false;
    return settings.appPerfMode[i] != PERF_INACTIVE;
}

int AppState::FindFirstEnabledTab() const
{
    for (int i = 0; i < GetTotalTabCount(); i++)
        if (IsTabEnabled(i)) return i;
    return 0;
}

// ============================================================================
// Settings
// ============================================================================

static std::wstring GetAppDataFolder()
{
    wchar_t* ad = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &ad)))
    {
        std::wstring f = std::wstring(ad) + L"\\Wallow";
        CoTaskMemFree(ad);
        CreateDirectoryW(f.c_str(), nullptr);
        return f;
    }
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    auto f = std::filesystem::path(p).parent_path() / L"Wallow_Data";
    CreateDirectoryW(f.wstring().c_str(), nullptr);
    return f.wstring();
}

void WAlowSettings::InitDefaults()
{
    for (int i = 0; i < BUILTIN_COUNT; i++)
        appPerfMode[i] = g_builtinApps[i].defaultPerf;
    for (int i = BUILTIN_COUNT; i < MAX_TABS; i++)
        appPerfMode[i] = PERF_INACTIVE;
}

void WAlowSettings::Load(const std::wstring& folder)
{
    InitDefaults();
    std::wstring path = folder + L"\\settings.ini";
    std::ifstream f(path);
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line))
    {
        if (line.rfind("ramLimitMB=", 0) == 0) ramLimitMB = (size_t)atoi(line.c_str() + 11);
        else if (line.rfind("diskCacheMB=", 0) == 0) diskCacheMB = (size_t)atoi(line.c_str() + 12);
        else if (line.rfind("showStatusBar=", 0) == 0) showStatusBar = atoi(line.c_str() + 14) != 0;
        else if (line.rfind("highDPI=", 0) == 0) highDPI = atoi(line.c_str() + 8) != 0;
        else if (line.rfind("zoomFactor=", 0) == 0) zoomFactor = (float)atof(line.c_str() + 11);
        else if (line.rfind("runAtStartup=", 0) == 0) runAtStartup = atoi(line.c_str() + 13) != 0;
        else if (line.rfind("customTabCount=", 0) == 0) customTabCount = atoi(line.c_str() + 15);
        else if (line.rfind("swapPath=", 0) == 0)
        {
            std::string v = line.substr(9);
            strncpy(swapPath, v.c_str(), sizeof(swapPath) - 1);
        }
        else if (line.rfind("perf_", 0) == 0)
        {
            auto eq = line.find('=');
            if (eq != std::string::npos)
            {
                int idx = atoi(line.c_str() + 5);
                if (idx >= 0 && idx < MAX_TABS)
                    appPerfMode[idx] = atoi(line.c_str() + eq + 1);
            }
        }
        else if (line.rfind("ctName_", 0) == 0)
        {
            auto eq = line.find('=');
            if (eq != std::string::npos)
            {
                int idx = atoi(line.c_str() + 7);
                if (idx >= 0 && idx < MAX_CUSTOM_TABS)
                    strncpy(customTabs[idx].name, line.c_str() + eq + 1, sizeof(customTabs[0].name) - 1);
            }
        }
        else if (line.rfind("ctUrl_", 0) == 0)
        {
            auto eq = line.find('=');
            if (eq != std::string::npos)
            {
                int idx = atoi(line.c_str() + 6);
                if (idx >= 0 && idx < MAX_CUSTOM_TABS)
                    strncpy(customTabs[idx].url, line.c_str() + eq + 1, sizeof(customTabs[0].url) - 1);
            }
        }
    }
    if (ramLimitMB < 64) ramLimitMB = 64;
    if (ramLimitMB > 4096) ramLimitMB = 4096;
    if (diskCacheMB < 20) diskCacheMB = 20;
    if (diskCacheMB > 2048) diskCacheMB = 2048;
    if (zoomFactor < 0.25f) zoomFactor = 0.25f;
    if (zoomFactor > 5.0f) zoomFactor = 5.0f;
    if (customTabCount < 0) customTabCount = 0;
    if (customTabCount > MAX_CUSTOM_TABS) customTabCount = MAX_CUSTOM_TABS;
    for (int i = 0; i < MAX_TABS; i++)
        if (appPerfMode[i] < 0 || appPerfMode[i] >= PERF_MODE_COUNT) appPerfMode[i] = PERF_INACTIVE;
}

void WAlowSettings::Save(const std::wstring& folder) const
{
    std::wstring path = folder + L"\\settings.ini";
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "ramLimitMB=" << ramLimitMB << "\n";
    f << "diskCacheMB=" << diskCacheMB << "\n";
    f << "swapPath=" << swapPath << "\n";
    f << "showStatusBar=" << (showStatusBar ? 1 : 0) << "\n";
    f << "highDPI=" << (highDPI ? 1 : 0) << "\n";
    f << "zoomFactor=" << zoomFactor << "\n";
    f << "runAtStartup=" << (runAtStartup ? 1 : 0) << "\n";
    f << "customTabCount=" << customTabCount << "\n";
    int total = BUILTIN_COUNT + customTabCount;
    for (int i = 0; i < total; i++)
        f << "perf_" << i << "=" << appPerfMode[i] << "\n";
    for (int i = 0; i < customTabCount; i++)
    {
        f << "ctName_" << i << "=" << customTabs[i].name << "\n";
        f << "ctUrl_" << i << "=" << customTabs[i].url << "\n";
    }
}

std::wstring WAlowSettings::GetEffectiveSwapPath(const std::wstring& def) const
{
    if (swapPath[0] != '\0')
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, swapPath, -1, nullptr, 0);
        std::wstring w(len - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, swapPath, -1, w.data(), len);
        CreateDirectoryW(w.c_str(), nullptr);
        return w;
    }
    std::wstring f = def + L"\\WebView2Data";
    CreateDirectoryW(f.c_str(), nullptr);
    return f;
}

// ============================================================================
// Run at startup (registry)
// ============================================================================

void SetRunAtStartup(bool enable)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
    {
        if (enable)
        {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            RegSetValueExW(key, L"Wallow", 0, REG_SZ, (BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
        }
        else
            RegDeleteValueW(key, L"Wallow");
        RegCloseKey(key);
    }
}

bool IsRunAtStartup()
{
    HKEY key;
    bool result = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        result = RegQueryValueExW(key, L"Wallow", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(key);
    }
    return result;
}

void BrowseForFolder(char* pathBuf, size_t bufSize, HWND owner)
{
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select storage folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl)
    {
        wchar_t wpath[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, wpath))
            WideCharToMultiByte(CP_UTF8, 0, wpath, -1, pathBuf, (int)bufSize, nullptr, nullptr);
        CoTaskMemFree(pidl);
    }
}

// ============================================================================
// System Tray
// ============================================================================

void CreateTrayIcon(AppState& app)
{
    app.nid.cbSize = sizeof(app.nid);
    app.nid.hWnd = app.hwnd;
    app.nid.uID = 1;
    app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    app.nid.uCallbackMessage = WM_TRAYICON;
    app.nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!app.nid.hIcon) app.nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(app.nid.szTip, L"Wallow");
    Shell_NotifyIconW(NIM_ADD, &app.nid);
    app.trayCreated = true;
}

void RemoveTrayIcon(AppState& app)
{
    if (app.trayCreated) { Shell_NotifyIconW(NIM_DELETE, &app.nid); app.trayCreated = false; }
}

void MinimizeToTray(AppState& app)
{
    ShowWindow(app.hwnd, SW_HIDE);
    for (int i = 0; i < app.GetTotalTabCount(); i++)
        if (app.tabs[i].controller) app.tabs[i].controller->put_IsVisible(FALSE);
}

void RestoreFromTray(AppState& app)
{
    ShowWindow(app.hwnd, SW_SHOW);
    SetForegroundWindow(app.hwnd);
    if (app.tabs[app.activeTab].controller)
        app.tabs[app.activeTab].controller->put_IsVisible(TRUE);
}

// ============================================================================
// Memory
// ============================================================================

void SetLowPriority() { SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS); }

void ApplyWorkingSetLimit(size_t limitMB)
{
    SIZE_T mn = 30ULL * 1024 * 1024, mx = limitMB * 1024ULL * 1024ULL;
    if (mx < mn) mx = mn;
    SetProcessWorkingSetSizeEx(GetCurrentProcess(), mn, mx, QUOTA_LIMITS_HARDWS_MAX_DISABLE);
}

void ApplyTabMemoryPolicy(AppState& app, int idx, bool isActive)
{
    auto& t = app.tabs[idx];
    if (!t.webview) return;
    ICoreWebView2_19* w19 = nullptr;
    if (SUCCEEDED(t.webview->QueryInterface(IID_PPV_ARGS(&w19))) && w19)
    {
        w19->put_MemoryUsageTargetLevel(isActive ?
            COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL :
            COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW);
        w19->Release();
    }
    if (!isActive && app.settings.appPerfMode[idx] == PERF_SUSPENDED)
    {
        ICoreWebView2_3* w3 = nullptr;
        if (SUCCEEDED(t.webview->QueryInterface(IID_PPV_ARGS(&w3))) && w3) { w3->TrySuspend(nullptr); w3->Release(); }
    }
    else if (isActive)
    {
        ICoreWebView2_3* w3 = nullptr;
        if (SUCCEEDED(t.webview->QueryInterface(IID_PPV_ARGS(&w3))) && w3) { w3->Resume(); w3->Release(); }
    }
}

void PeriodicMemoryMaintenance(AppState& app)
{
    DWORD now = GetTickCount();
    if (now - app.lastTrimTime < 15000) return;
    app.lastTrimTime = now;
    ApplyWorkingSetLimit(app.settings.ramLimitMB);
    PROCESS_MEMORY_COUNTERS_EX pmc = {}; pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
        app.memoryUsageMB = pmc.WorkingSetSize / (1024 * 1024);
    for (int i = 0; i < app.GetTotalTabCount(); i++)
        if (app.IsTabEnabled(i)) ApplyTabMemoryPolicy(app, i, i == app.activeTab);
    std::wstring sf = app.settings.GetEffectiveSwapPath(app.appDataFolder);
    ULONGLONG tb = 0; WIN32_FIND_DATAW fd;
    HANDLE hF = FindFirstFileW((sf + L"\\*").c_str(), &fd);
    if (hF != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            tb += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        } while (FindNextFileW(hF, &fd)); FindClose(hF);
    }
    app.swapFileSizeMB = (size_t)(tb / (1024 * 1024));
}

// ============================================================================
// DirectX 11
// ============================================================================

bool CreateDeviceD3D(HWND hWnd, AppState& app)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate = { 60, 1 };
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl; const D3D_FEATURE_LEVEL ls[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        ls, 2, D3D11_SDK_VERSION, &sd, &app.swapChain, &app.d3dDevice, &fl, &app.d3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            ls, 2, D3D11_SDK_VERSION, &sd, &app.swapChain, &app.d3dDevice, &fl, &app.d3dDeviceContext);
    if (FAILED(hr)) return false;
    CreateRenderTarget(app); return true;
}

void CleanupRenderTarget(AppState& a) { if (a.mainRenderTargetView) { a.mainRenderTargetView->Release(); a.mainRenderTargetView = nullptr; } }
void CreateRenderTarget(AppState& a) { ID3D11Texture2D* b = nullptr; a.swapChain->GetBuffer(0, IID_PPV_ARGS(&b)); if (b) { a.d3dDevice->CreateRenderTargetView(b, nullptr, &a.mainRenderTargetView); b->Release(); } }
void CleanupDeviceD3D(AppState& a) { CleanupRenderTarget(a); if (a.swapChain) { a.swapChain->Release(); a.swapChain = nullptr; } if (a.d3dDeviceContext) { a.d3dDeviceContext->Release(); a.d3dDeviceContext = nullptr; } if (a.d3dDevice) { a.d3dDevice->Release(); a.d3dDevice = nullptr; } }

// ============================================================================
// WebView2 zoom
// ============================================================================

void ApplyZoomToTab(AppState& app, int idx)
{
    auto& t = app.tabs[idx];
    if (!t.controller) return;
    ICoreWebView2Controller* ctrl = t.controller;
    ctrl->put_ZoomFactor((double)app.settings.zoomFactor);
}

// ============================================================================
// WebView2 multi-tab
// ============================================================================

static void SetupTabWebView(AppState& app, int idx, ICoreWebView2Controller* ctrl)
{
    auto& t = app.tabs[idx];
    t.controller = ctrl; ctrl->AddRef();
    ctrl->get_CoreWebView2(&t.webview);

    ComPtr<ICoreWebView2Settings> s;
    t.webview->get_Settings(&s);
    if (s) { s->put_IsScriptEnabled(TRUE); s->put_AreDefaultScriptDialogsEnabled(TRUE);
        s->put_IsWebMessageEnabled(TRUE); s->put_AreDevToolsEnabled(FALSE);
        s->put_IsStatusBarEnabled(FALSE); s->put_AreDefaultContextMenusEnabled(TRUE); }

    ICoreWebView2Settings2* s2 = nullptr;
    if (SUCCEEDED(s->QueryInterface(IID_PPV_ARGS(&s2))) && s2) {
        s2->put_UserAgent(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        s2->Release();
    }

    ICoreWebView2Controller2* c2 = nullptr;
    if (SUCCEEDED(ctrl->QueryInterface(IID_PPV_ARGS(&c2))) && c2) {
        COREWEBVIEW2_COLOR bg = { 255, 10, 10, 10 };
        c2->put_DefaultBackgroundColor(bg); c2->Release();
    }

    t.webview->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
        [&t](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) -> HRESULT { t.loading = true; return S_OK; }).Get(), nullptr);

    t.webview->add_NavigationCompleted(Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [&t](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
            t.loading = false; LPWSTR title = nullptr; sender->get_DocumentTitle(&title);
            if (title) { int l = WideCharToMultiByte(CP_UTF8, 0, title, -1, nullptr, 0, nullptr, nullptr);
                t.title.resize(l - 1); WideCharToMultiByte(CP_UTF8, 0, title, -1, t.title.data(), l, nullptr, nullptr);
                CoTaskMemFree(title); }
            return S_OK; }).Get(), nullptr);

    t.webview->add_NewWindowRequested(Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            // Force new windows to open in the current WebView
            args->put_Handled(TRUE);
            LPWSTR uri = nullptr;
            args->get_Uri(&uri);
            if (uri) {
                sender->Navigate(uri);
                CoTaskMemFree(uri);
            }
            return S_OK;
        }).Get(), nullptr);

    t.webview->Navigate(app.GetTabUrl(idx).c_str());
    t.ready = true;

    ctrl->put_ZoomFactor((double)app.settings.zoomFactor);

    bool isActive = (idx == app.activeTab);
    ApplyTabMemoryPolicy(app, idx, isActive);
    if (!isActive) { RECT e = { 0,0,0,0 }; ctrl->put_Bounds(e); ctrl->put_IsVisible(FALSE); }
    else ResizeWebView(app);
}

void CreateTabWebView(AppState& app, int idx)
{
    if (app.tabInitStarted[idx]) return;
    if (!app.IsTabEnabled(idx)) return;
    app.tabInitStarted[idx] = true;
    if (!app.webviewEnvironment) return;
    int i = idx;
    app.webviewEnvironment->CreateCoreWebView2Controller(app.hwnd,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [i](HRESULT r, ICoreWebView2Controller* c) -> HRESULT {
                if (FAILED(r) || !c) return r;
                SetupTabWebView(g_app, i, c); return S_OK;
            }).Get());
}

void InitWebView2Environment(AppState& app)
{
    std::wstring udf = app.settings.GetEffectiveSwapPath(app.appDataFolder);
    auto eo = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    wchar_t args[512];
    swprintf_s(args, 512, L"--enable-low-end-device-mode --disk-cache-size=%zu --disable-features=BackForwardCache --disable-site-isolation-trials",
        app.settings.diskCacheMB * 1024ULL * 1024ULL);
    eo->put_AdditionalBrowserArguments(args);
    eo->put_AllowSingleSignOnUsingOSPrimaryAccount(TRUE);
    
    CreateCoreWebView2EnvironmentWithOptions(nullptr, udf.c_str(), eo.Get(),
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&app](HRESULT r, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(r) || !env) return r;
                app.webviewEnvironment = env; env->AddRef();
                CreateTabWebView(app, app.activeTab);
                return S_OK;
            }).Get());
}

void SwitchTab(AppState& app, int newTab)
{
    if (newTab == app.activeTab) return;
    if (!app.IsTabEnabled(newTab)) return;
    int old = app.activeTab;
    if (app.tabs[old].controller) {
        RECT e = { 0,0,0,0 }; app.tabs[old].controller->put_Bounds(e);
        app.tabs[old].controller->put_IsVisible(FALSE);
        ApplyTabMemoryPolicy(app, old, false);
    }
    app.activeTab = newTab;
    if (!app.tabInitStarted[newTab]) CreateTabWebView(app, newTab);
    if (app.tabs[newTab].controller) {
        app.tabs[newTab].controller->put_IsVisible(TRUE);
        ApplyTabMemoryPolicy(app, newTab, true);
        ResizeWebView(app);
    }
}

void ResizeWebView(AppState& app)
{
    auto& t = app.tabs[app.activeTab];
    if (!t.controller) return;
    if (app.showSettings) { RECT e = { 0,0,0,0 }; t.controller->put_Bounds(e); return; }
    RECT b; GetClientRect(app.hwnd, &b);
    b.top = TOOLBAR_HEIGHT;
    if (app.settings.showStatusBar) b.bottom -= STATUSBAR_HEIGHT;
    t.controller->put_Bounds(b);
}

// ============================================================================
// ImGui UI
// ============================================================================

static void RenderToolbar(AppState& app)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, (float)TOOLBAR_HEIGHT));
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.039f, 0.039f, 0.039f, 1.0f));
    ImGui::Begin("##Toolbar", nullptr, fl);

    bool first = true;
    for (int i = 0; i < app.GetTotalTabCount(); i++)
    {
        if (!app.IsTabEnabled(i)) continue;
        if (!first) ImGui::SameLine();
        first = false;
        bool active = (i == app.activeTab) && !app.showSettings;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.12f, 0.12f, 0.12f, 1.0f) : ImVec4(0.055f, 0.055f, 0.055f, 1.0f));
        char lbl[80];
        snprintf(lbl, sizeof(lbl), app.tabs[i].loading ? "%s *" : "%s", app.GetTabName(i));
        if (ImGui::Button(lbl, ImVec2(0, 22)))
        {
            if (app.showSettings) { app.showSettings = false; }
            SwitchTab(app, i);
            ResizeWebView(app);
        }
        ImGui::PopStyleColor();
    }
    if (!first) ImGui::SameLine();

    float sw = ImGui::CalcTextSize("Settings").x + ImGui::GetStyle().FramePadding.x * 2;
    float rx = io.DisplaySize.x - sw - 10.0f;
    ImGui::SameLine(rx);
    if (app.showSettings) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("Settings", ImVec2(0, 22))) { app.showSettings = false; ResizeWebView(app); }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Settings", ImVec2(0, 22))) { app.showSettings = true; ResizeWebView(app); }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

static void RenderSettingsPanel(AppState& app)
{
    if (!app.showSettings) return;
    ImGuiIO& io = ImGui::GetIO();
    float py = (float)TOOLBAR_HEIGHT, ph = io.DisplaySize.y - py;
    if (app.settings.showStatusBar) ph -= (float)STATUSBAR_HEIGHT;
    ImGui::SetNextWindowPos(ImVec2(0, py));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, ph));
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.045f, 0.045f, 1.0f));
    ImGui::Begin("##Settings", nullptr, fl);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "Settings");
    ImGui::Separator(); ImGui::Spacing();

    // Memory
    ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.5f, 1.0f), "Memory");
    ImGui::Spacing();
    int rl = (int)app.settings.ramLimitMB;
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputInt("RAM Limit (MB)", &rl)) {
        if (rl < 64) rl = 64; if (rl > 4096) rl = 4096;
        app.settings.ramLimitMB = (size_t)rl; ApplyWorkingSetLimit(rl);
    }
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Current: %zuMB in RAM, %zuMB on disk", app.memoryUsageMB, app.swapFileSizeMB);
    ImGui::Spacing();
    int dc = (int)app.settings.diskCacheMB;
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputInt("Disk Cache (MB)", &dc)) {
        if (dc < 20) dc = 20; if (dc > 2048) dc = 2048;
        app.settings.diskCacheMB = (size_t)dc;
    }
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Applied on restart");
    ImGui::Spacing(); ImGui::Spacing();

    // Apps & Performance
    ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.5f, 1.0f), "Apps (set Inactive to hide)");
    ImGui::Spacing();
    for (int i = 0; i < app.GetTotalTabCount(); i++)
    {
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(140);
        ImGui::Combo(app.GetTabName(i), &app.settings.appPerfMode[i], PerfModeNames, PERF_MODE_COUNT);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Reset & Clear Data"))
        {
            if (app.tabs[i].webview) {
                // Try to clear browsing data (cookies, storage, etc.)
                ICoreWebView2_13* wv13 = nullptr;
                if (SUCCEEDED(app.tabs[i].webview->QueryInterface(IID_PPV_ARGS(&wv13))) && wv13) {
                    ICoreWebView2Profile* profile = nullptr;
                    wv13->get_Profile(&profile);
                    if (profile) {
                        ICoreWebView2Profile2* profile2 = nullptr;
                        if (SUCCEEDED(profile->QueryInterface(IID_PPV_ARGS(&profile2))) && profile2) {
                            // Clear all browsing data (cookies, dom storage, etc.)
                            profile2->ClearBrowsingDataInTimeRange(
                                (COREWEBVIEW2_BROWSING_DATA_KINDS)(COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_DOM_STORAGE | COREWEBVIEW2_BROWSING_DATA_KINDS_COOKIES),
                                0, 3155378976000000000LL, nullptr); // ~100 years
                            profile2->Release();
                        }
                        profile->Release();
                    }
                    wv13->Release();
                }
            }

            if (app.tabs[i].controller) {
                app.tabs[i].controller->Close();
                app.tabs[i].controller->Release();
                app.tabs[i].controller = nullptr;
                app.tabs[i].webview = nullptr;
            }
            app.tabs[i].ready = false;
            app.tabs[i].loading = false;
            app.tabInitStarted[i] = false; // Allow re-init
            if (i == app.activeTab) CreateTabWebView(app, i);
        }
        ImGui::PopStyleColor(3);
        ImGui::PopID();
    }
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
        "Normal/Low/Suspend = behavior when not the active tab. Inactive = completely off.");
    ImGui::Spacing(); ImGui::Spacing();

    // Custom tabs
    ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.5f, 1.0f), "Custom Tabs");
    ImGui::Spacing();
    for (int i = 0; i < app.settings.customTabCount; i++)
    {
        ImGui::PushID(1000 + i);
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("Name", app.settings.customTabs[i].name, sizeof(app.settings.customTabs[0].name));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("URL", app.settings.customTabs[i].url, sizeof(app.settings.customTabs[0].url));
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
        {
            for (int j = i; j < app.settings.customTabCount - 1; j++)
                app.settings.customTabs[j] = app.settings.customTabs[j + 1];
            app.settings.customTabCount--;
            for (int j = BUILTIN_COUNT + i; j < BUILTIN_COUNT + app.settings.customTabCount; j++)
                app.settings.appPerfMode[j] = app.settings.appPerfMode[j + 1];
            app.settings.appPerfMode[BUILTIN_COUNT + app.settings.customTabCount] = PERF_INACTIVE;
            i--;
        }
        ImGui::PopID();
    }
    if (app.settings.customTabCount < MAX_CUSTOM_TABS)
    {
        if (ImGui::Button("+ Add Custom Tab"))
        {
            int ci = app.settings.customTabCount;
            strncpy(app.settings.customTabs[ci].name, "New", sizeof(app.settings.customTabs[0].name));
            strncpy(app.settings.customTabs[ci].url, "https://", sizeof(app.settings.customTabs[0].url));
            app.settings.appPerfMode[BUILTIN_COUNT + ci] = PERF_INACTIVE;
            app.settings.customTabCount++;
        }
    }
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "New/changed custom tabs need app restart. Set perf mode above.");
    ImGui::Spacing(); ImGui::Spacing();

    // Display
    ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.5f, 1.0f), "Display");
    ImGui::Spacing();

    /*
    // Removed Zoom option as per request
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat("Page Zoom", &app.settings.zoomFactor, 0.25f, 3.0f, "%.0f%%"))
    {
        for (int i = 0; i < app.GetTotalTabCount(); i++)
            ApplyZoomToTab(app, i);
    }
    // Show percentage properly
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "(%.0f%%)", app.settings.zoomFactor * 100.0f);
    */

    bool sb = app.settings.showStatusBar;
    if (ImGui::Checkbox("Show Status Bar", &sb)) app.settings.showStatusBar = sb;
    
    bool hdpi = app.settings.highDPI;
    if (ImGui::Checkbox("High DPI Rendering (Crisp)", &hdpi)) app.settings.highDPI = hdpi;
    if (hdpi != (IsProcessDPIAware() ? true : false)) // Note: Simplistic check, restart simplifies logic
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Restart required to apply DPI change.");
    ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Note: Image colors may look off like the bit depth is low.");

    ImGui::Spacing(); ImGui::Spacing();

    // Storage
    ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.5f, 1.0f), "Storage Location");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(350);
    ImGui::InputText("##SwapPath", app.settings.swapPath, sizeof(app.settings.swapPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
        BrowseForFolder(app.settings.swapPath, sizeof(app.settings.swapPath), app.hwnd);
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Leave empty for default. Change needs restart.");
    std::wstring eff = app.settings.GetEffectiveSwapPath(app.appDataFolder);
    char eu[512]; WideCharToMultiByte(CP_UTF8, 0, eff.c_str(), -1, eu, sizeof(eu), nullptr, nullptr);
    ImGui::TextColored(ImVec4(0.25f, 0.50f, 0.40f, 1.0f), "Active: %s", eu);
    ImGui::Spacing(); ImGui::Spacing();

    // Startup
    ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.5f, 1.0f), "System");
    ImGui::Spacing();
    bool ras = app.settings.runAtStartup;
    if (ImGui::Checkbox("Run at Windows startup", &ras))
    {
        app.settings.runAtStartup = ras;
        SetRunAtStartup(ras);
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    if (ImGui::Button("Save & Close", ImVec2(150, 30)))
    {
        app.settings.Save(app.appDataFolder);
        ApplyWorkingSetLimit(app.settings.ramLimitMB);
        // If active tab became inactive, switch
        if (!app.IsTabEnabled(app.activeTab))
        {
            int nt = app.FindFirstEnabledTab();
            app.activeTab = nt;
        }
        app.showSettings = false;
        ResizeWebView(app);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

static void RenderStatusBar(AppState& app)
{
    if (!app.settings.showStatusBar) return;
    ImGuiIO& io = ImGui::GetIO();
    float y = io.DisplaySize.y - (float)STATUSBAR_HEIGHT;
    ImGui::SetNextWindowPos(ImVec2(0, y));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, (float)STATUSBAR_HEIGHT));
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.035f, 0.035f, 1.0f));
    ImGui::Begin("##StatusBar", nullptr, fl);
    auto& t = app.tabs[app.activeTab];
    if (t.loading) ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.5f, 1.0f), "%s - Loading...", app.GetTabName(app.activeTab));
    else if (!t.title.empty()) ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "%s", t.title.c_str());
    else ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "%s", app.GetTabName(app.activeTab));
    ImGui::SameLine(io.DisplaySize.x - 220.0f);
    ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.35f, 1.0f), "RAM: %zuMB / %zuMB  Disk: %zuMB",
        app.memoryUsageMB, app.settings.ramLimitMB, app.swapFileSizeMB);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// WndProc
// ============================================================================

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg)
    {
    case WM_SIZE:
        if (g_app.d3dDevice && wParam != SIZE_MINIMIZED) {
            g_app.windowWidth = LOWORD(lParam); g_app.windowHeight = HIWORD(lParam);
            CleanupRenderTarget(g_app);
            g_app.swapChain->ResizeBuffers(0, g_app.windowWidth, g_app.windowHeight, DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget(g_app); ResizeWebView(g_app);
        } return 0;
    case WM_CLOSE: MinimizeToTray(g_app); return 0;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) RestoreFromTray(g_app);
        else if (lParam == WM_RBUTTONUP) {
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1, L"Show Wallow");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, 2, L"Quit");
            POINT pt; GetCursorPos(&pt); SetForegroundWindow(hWnd);
            int c = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(m);
            if (c == 1) RestoreFromTray(g_app);
            else if (c == 2) { g_app.wantQuit = true; DestroyWindow(hWnd); }
        } return 0;
    case WM_SYSCOMMAND: if ((wParam & 0xfff0) == SC_KEYMENU) return 0; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// Entry
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow)
{
    SetLowPriority();
    ULONG hi = 2; HeapSetInformation(GetProcessHeap(), HeapCompatibilityInformation, &hi, sizeof(hi));
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_app.appDataFolder = GetAppDataFolder();
    g_app.settings.Load(g_app.appDataFolder);
    if (g_app.settings.highDPI) ImGui_ImplWin32_EnableDpiAwareness();

    g_app.settings.runAtStartup = IsRunAtStartup();
    ApplyWorkingSetLimit(g_app.settings.ramLimitMB);
    g_app.activeTab = g_app.FindFirstEnabledTab();

    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"Wallow_Class";
    RegisterClassExW(&wc);

    g_app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Wallow", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, g_app.windowWidth, g_app.windowHeight, nullptr, nullptr, hInst, nullptr);
    BOOL dark = TRUE; DwmSetWindowAttribute(g_app.hwnd, 20, &dark, sizeof(dark));

    if (!CreateDeviceD3D(g_app.hwnd, g_app)) { CleanupDeviceD3D(g_app); UnregisterClassW(wc.lpszClassName, hInst); return 1; }

    ShowWindow(g_app.hwnd, nShow); UpdateWindow(g_app.hwnd);
    CreateTrayIcon(g_app);

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0.0f; st.FrameRounding = 3.0f; st.GrabRounding = 3.0f;
    st.FramePadding = ImVec2(6, 3); st.ItemSpacing = ImVec2(6, 4);
    st.Colors[ImGuiCol_WindowBg]         = ImVec4(0.039f, 0.039f, 0.039f, 1.0f);
    st.Colors[ImGuiCol_ChildBg]          = ImVec4(0.039f, 0.039f, 0.039f, 1.0f);
    st.Colors[ImGuiCol_PopupBg]          = ImVec4(0.06f, 0.06f, 0.06f, 1.0f);
    st.Colors[ImGuiCol_FrameBg]          = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
    st.Colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
    st.Colors[ImGuiCol_FrameBgActive]    = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    st.Colors[ImGuiCol_TitleBg]          = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
    st.Colors[ImGuiCol_TitleBgActive]    = ImVec4(0.06f, 0.06f, 0.06f, 1.0f);
    st.Colors[ImGuiCol_Button]           = ImVec4(0.07f, 0.07f, 0.07f, 1.0f);
    st.Colors[ImGuiCol_ButtonHovered]    = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
    st.Colors[ImGuiCol_ButtonActive]     = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    st.Colors[ImGuiCol_Header]           = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
    st.Colors[ImGuiCol_HeaderHovered]    = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
    st.Colors[ImGuiCol_HeaderActive]     = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    st.Colors[ImGuiCol_SliderGrab]       = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    st.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
    st.Colors[ImGuiCol_CheckMark]        = ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
    st.Colors[ImGuiCol_Text]             = ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
    st.Colors[ImGuiCol_TextDisabled]     = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
    st.Colors[ImGuiCol_Separator]        = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    st.Colors[ImGuiCol_SeparatorActive]  = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    st.Colors[ImGuiCol_Border]           = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);

    ImGui_ImplWin32_Init(g_app.hwnd);
    ImGui_ImplDX11_Init(g_app.d3dDevice, g_app.d3dDeviceContext);
    InitWebView2Environment(g_app);

    bool done = false; MSG msg;
    while (!done)
    {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (!IsWindowVisible(g_app.hwnd)) { Sleep(100); continue; }
        PeriodicMemoryMaintenance(g_app);

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        RenderToolbar(g_app); RenderSettingsPanel(g_app); RenderStatusBar(g_app);
        ImGui::Render();
        const float cc[4] = { 0.039f, 0.039f, 0.039f, 1.0f };
        g_app.d3dDeviceContext->OMSetRenderTargets(1, &g_app.mainRenderTargetView, nullptr);
        g_app.d3dDeviceContext->ClearRenderTargetView(g_app.mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_app.swapChain->Present(1, 0);
    }

    g_app.settings.Save(g_app.appDataFolder);
    RemoveTrayIcon(g_app);
    for (int i = 0; i < g_app.GetTotalTabCount(); i++)
        if (g_app.tabs[i].controller) { g_app.tabs[i].controller->Close(); g_app.tabs[i].controller->Release(); }
    if (g_app.webviewEnvironment) g_app.webviewEnvironment->Release();

    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupDeviceD3D(g_app); DestroyWindow(g_app.hwnd);
    UnregisterClassW(wc.lpszClassName, hInst); CoUninitialize();
    return 0;
}
