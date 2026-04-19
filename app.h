#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#ifndef EM_SETBKGNDCOLOR
#define EM_SETBKGNDCOLOR 0x0443
#endif

constexpr int ID_BTN_CHECK   = 101;
constexpr int ID_BTN_FLASH   = 102;
constexpr int ID_PROGRESS    = 103;
constexpr int ID_LOG_WINDOW  = 104;
constexpr int ID_LBL_STATUS  = 105;
constexpr int ID_LBL_DEVICE  = 106;
constexpr int ID_LBL_FASTBT  = 107;
constexpr int ID_LBL_ROM     = 108;
constexpr int ID_LBL_STEPS   = 109;
constexpr int ID_LBL_HINT    = 110;
constexpr int ID_LBL_TITLE   = 111;
constexpr int ID_LBL_SUB     = 112;
constexpr int ID_BTN_OPTION  = 113;
constexpr int ID_LBL_BGIMG   = 114;

constexpr UINT WM_LOG_FLUSH = WM_APP + 1;
constexpr UINT WM_PROG_SET  = WM_APP + 2;
constexpr UINT WM_OP_DONE   = WM_APP + 3;
constexpr UINT WM_TEXT_SET  = WM_APP + 4;

extern const wchar_t APP_CLASS[];

struct ThemePalette {
    COLORREF bg{};
    COLORREF bg2{};
    COLORREF panel{};
    COLORREF panel2{};
    COLORREF line{};
    COLORREF text{};
    COLORREF muted{};
    COLORREF accent{};
    COLORREF success{};
    COLORREF warning{};
    COLORREF danger{};
    COLORREF btn{};
    COLORREF btnHot{};
    COLORREF btnDown{};
    COLORREF btnEdge{};
    COLORREF editBg{};
    COLORREF chipBg{};
    COLORREF chipEdge{};
};

struct AppConfig {
    int themeIndex{};
    std::wstring imageDir;
    std::wstring backgroundImage;
};

#define C_BG        (g_theme.bg)
#define C_BG2       (g_theme.bg2)
#define C_PANEL     (g_theme.panel)
#define C_PANEL2    (g_theme.panel2)
#define C_LINE      (g_theme.line)
#define C_TEXT      (g_theme.text)
#define C_MUTED     (g_theme.muted)
#define C_ACCENT    (g_theme.accent)
#define C_SUCCESS   (g_theme.success)
#define C_WARNING   (g_theme.warning)
#define C_DANGER    (g_theme.danger)
#define C_BTN       (g_theme.btn)
#define C_BTN_HOV   (g_theme.btnHot)
#define C_BTN_DN    (g_theme.btnDown)
#define C_BTN_EDGE  (g_theme.btnEdge)
#define C_EDIT_BG   (g_theme.editBg)

struct ExecResult {
    DWORD exitCode{};
    std::string output;
    std::wstring launchError;
};

struct FlashStep {
    std::string cmd;
    std::wstring desc;
    const char* asset;
};

struct TextMessage {
    uint32_t token{};
    UINT kind{};
    std::wstring text;
};

struct LogLine {
    uint32_t token{};
    std::wstring text;
};

struct Layout {
    RECT header{};
    RECT leftCard{};
    RECT rightCard{};
    RECT logCard{};
};

extern HWND g_hMain;
extern HWND g_hSettings;
extern HWND hBtnCheck;
extern HWND hBtnFlash;
extern HWND hBtnOption;
extern HWND hProgressBar;
extern HWND hLog;
extern HWND hLblStatus;
extern HWND hLblDevice;
extern HWND hLblFastboot;
extern HWND hLblRom;
extern HWND hLblSteps;
extern HWND hLblHint;
extern HWND hLblTitle;
extern HWND hLblSub;
extern HWND hLblBgImage;

extern HFONT g_hFontTitle;
extern HFONT g_hFontSub;
extern HFONT g_hFontBody;
extern HFONT g_hFontMono;

extern HBRUSH g_brPanel;
extern HBRUSH g_brPanel2;
extern HBRUSH g_brEdit;
extern HBRUSH g_brTransparent;

extern ThemePalette g_theme;
extern AppConfig g_Config;
extern Gdiplus::Bitmap* g_BackgroundBitmap;
extern ULONG_PTR g_GdiplusToken;

extern std::atomic<bool> g_DeviceVerified;
extern std::atomic<bool> g_Busy;
extern std::atomic<bool> g_Unlocked;
extern std::atomic<uint32_t> g_CurrentOperationToken;

extern std::wstring g_StatusText;
extern std::wstring g_DeviceText;
extern std::wstring g_FastbootText;
extern std::wstring g_RomText;
extern std::wstring g_StepsText;
extern std::wstring g_HintText;
extern std::wstring g_BgImageText;

extern Layout g_layout;
extern std::mutex g_LogMutex;
extern std::deque<LogLine> g_LogQueue;
extern std::atomic<bool> g_LogFlushPending;

void SafeDeleteObject(HGDIOBJ obj);
std::wstring ToWide(const std::string& s);
std::string WideToAnsi(const std::wstring& s);
std::wstring Win32ErrorText(DWORD err);
bool FileExistsA(const std::string& path);
bool FileExistsW(const std::wstring& path);
std::wstring ModuleDirW();
std::wstring ConfigPathW();
std::string RomDir();
std::string FASTBOOT_EXE();
std::string FB(const std::string& args);
std::string AssetPath(const char* filename);
std::string Img(const char* filename);
ExecResult Exec(const std::string& cmdLine);
HFONT MakeFont(const wchar_t* face, int size, int weight = FW_NORMAL, BOOL italic = FALSE);
uint32_t BeginOperation();
void QueueLog(uint32_t token, const std::wstring& msg);
void QueueText(uint32_t token, UINT kind, const std::wstring& msg);
void QueueProgress(uint32_t token, WORD pos, WORD rng);
void QueueDone(uint32_t token, bool ok, bool flashMode);
void UpdateText(HWND hwnd, const std::wstring& text);
void UpdateStatusUI(const std::wstring& text);
void UpdateDeviceUI(const std::wstring& text);
void UpdateHintUI(const std::wstring& text);
void UpdateStepsUI(const std::wstring& text);
void UpdateBgImageUI(const std::wstring& text);
void LayoutControls(HWND hwnd);
void DrawRoundCard(HDC hdc, const RECT& rc, COLORREF fill, COLORREF edge, int radius = 18);
void DrawChip(HDC hdc, int x, int y, int w, int h, COLORREF fill, COLORREF edge, const wchar_t* text);
void PaintMain(HDC hdc, const RECT& rc);
void DrawButtonFace(LPDRAWITEMSTRUCT dis, bool hot, bool pressed, bool enabled);
void AppendLogBlock(const std::wstring& text);
void FlushLogQueue();
void CleanupGdi();
std::vector<FlashStep> BuildFlashSteps();
void CheckThread(uint32_t token);
void FlashThread(uint32_t token);
bool InitGdiPlus();
void ShutdownGdiPlus();
void ReloadBackgroundImage();
void ApplyAppConfig(const AppConfig& cfg);
void OpenSettingsWindow(HWND owner);
AppConfig LoadAppConfig();
void SaveAppConfig(const AppConfig& cfg);
AppConfig DefaultAppConfig();
