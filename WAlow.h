#pragma once

#include <string>
#include <vector>
#include <d3d11.h>
#include <shellapi.h>

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;

#define WM_TRAYICON (WM_USER + 1)
#define MAX_TABS 32
#define MAX_CUSTOM_TABS 16

enum PerfMode
{
    PERF_NORMAL = 0,
    PERF_LOW,
    PERF_SUSPENDED,
    PERF_INACTIVE,
    PERF_MODE_COUNT
};

static const char* PerfModeNames[] = { "Normal", "Low Memory", "Suspend", "Inactive" };

struct BuiltinApp
{
    const char*    name;
    const wchar_t* url;
    int            defaultPerf;
};

static const BuiltinApp g_builtinApps[] = {
    { "WhatsApp",  L"https://web.whatsapp.com",  PERF_NORMAL },
    { "Instagram", L"https://www.instagram.com",  PERF_LOW },
    { "Telegram",  L"https://web.telegram.org",   PERF_INACTIVE },
    { "TikTok",    L"https://www.tiktok.com",     PERF_INACTIVE },
    { "LinkedIn",  L"https://www.linkedin.com",   PERF_INACTIVE },
    { "Facebook",  L"https://www.facebook.com",   PERF_INACTIVE },
};
static const int BUILTIN_COUNT = sizeof(g_builtinApps) / sizeof(g_builtinApps[0]);

struct CustomTab
{
    char name[64] = "";
    char url[512] = "";
};

struct WAlowSettings
{
    size_t      ramLimitMB = 512;
    char        swapPath[512] = "";
    size_t      diskCacheMB = 100;
    bool        showStatusBar = true;
    bool        highDPI = true;
    float       zoomFactor = 1.0f;
    bool        runAtStartup = false;
    int         appPerfMode[MAX_TABS] = {};
    CustomTab   customTabs[MAX_CUSTOM_TABS] = {};
    int         customTabCount = 0;

    void        Load(const std::wstring& folder);
    void        Save(const std::wstring& folder) const;
    std::wstring GetEffectiveSwapPath(const std::wstring& defaultFolder) const;
    void        InitDefaults();
};

struct TabState
{
    ICoreWebView2Controller*    controller = nullptr;
    ICoreWebView2*              webview = nullptr;
    bool                        ready = false;
    bool                        loading = false;
    std::string                 title;
};

struct AppState
{
    HWND                        hwnd = nullptr;
    int                         windowWidth = 1100;
    int                         windowHeight = 700;

    ID3D11Device*               d3dDevice = nullptr;
    ID3D11DeviceContext*        d3dDeviceContext = nullptr;
    IDXGISwapChain*             swapChain = nullptr;
    ID3D11RenderTargetView*     mainRenderTargetView = nullptr;

    ICoreWebView2Environment*   webviewEnvironment = nullptr;

    TabState                    tabs[MAX_TABS];
    int                         activeTab = 0;
    bool                        tabInitStarted[MAX_TABS] = {};

    bool                        showSettings = false;

    NOTIFYICONDATAW             nid = {};
    bool                        trayCreated = false;
    bool                        wantQuit = false;

    WAlowSettings               settings;
    std::wstring                appDataFolder;

    DWORD                       lastTrimTime = 0;
    size_t                      memoryUsageMB = 0;
    size_t                      swapFileSizeMB = 0;

    int     GetTotalTabCount() const;
    const char* GetTabName(int index) const;
    std::wstring GetTabUrl(int index) const;
    bool    IsTabEnabled(int index) const;
    int     FindFirstEnabledTab() const;
};

bool CreateDeviceD3D(HWND hWnd, AppState& app);
void CleanupDeviceD3D(AppState& app);
void CreateRenderTarget(AppState& app);
void CleanupRenderTarget(AppState& app);

void InitWebView2Environment(AppState& app);
void CreateTabWebView(AppState& app, int tabIndex);
void ResizeWebView(AppState& app);
void SwitchTab(AppState& app, int newTab);
void ApplyZoomToTab(AppState& app, int tabIndex);

void ApplyWorkingSetLimit(size_t limitMB);
void SetLowPriority();
void PeriodicMemoryMaintenance(AppState& app);
void ApplyTabMemoryPolicy(AppState& app, int tabIndex, bool isActive);

void CreateTrayIcon(AppState& app);
void RemoveTrayIcon(AppState& app);
void MinimizeToTray(AppState& app);
void RestoreFromTray(AppState& app);

void SetRunAtStartup(bool enable);
bool IsRunAtStartup();
void BrowseForFolder(char* pathBuf, size_t bufSize, HWND owner);
