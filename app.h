#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <atomic>

#ifndef EM_SETBKGNDCOLOR
#define EM_SETBKGNDCOLOR 0x0443
#endif

constexpr int ID_BTN_CHECK  = 101;
constexpr int ID_BTN_FLASH  = 102;
constexpr int ID_PROGRESS   = 103;
constexpr int ID_LOG_WINDOW = 104;
constexpr int ID_LBL_STATUS = 105;
constexpr int ID_LBL_DEVICE = 106;
constexpr int ID_LBL_FASTBT = 107;
constexpr int ID_LBL_ROM    = 108;
constexpr int ID_LBL_STEPS  = 109;
constexpr int ID_LBL_HINT   = 110;
constexpr int ID_LBL_TITLE  = 111;
constexpr int ID_LBL_SUB    = 112;

constexpr UINT WM_LOG_POST  = WM_APP + 1;
constexpr UINT WM_PROG_SET  = WM_APP + 2;
constexpr UINT WM_OP_DONE   = WM_APP + 3;
constexpr UINT WM_TEXT_SET  = WM_APP + 4;

extern const wchar_t APP_CLASS[];

constexpr COLORREF C_BG       = RGB(15, 18, 28);
constexpr COLORREF C_BG2      = RGB(21, 26, 40);
constexpr COLORREF C_PANEL    = RGB(25, 31, 48);
constexpr COLORREF C_PANEL2   = RGB(30, 38, 58);
constexpr COLORREF C_LINE     = RGB(56, 69, 98);
constexpr COLORREF C_TEXT     = RGB(237, 242, 247);
constexpr COLORREF C_MUTED    = RGB(155, 168, 190);
constexpr COLORREF C_ACCENT   = RGB(0, 179, 255);
constexpr COLORREF C_SUCCESS  = RGB(52, 199, 89);
constexpr COLORREF C_WARNING  = RGB(255, 185, 0);
constexpr COLORREF C_DANGER   = RGB(255, 92, 92);
constexpr COLORREF C_BTN      = RGB(39, 49, 72);
constexpr COLORREF C_BTN_HOV  = RGB(52, 63, 92);
constexpr COLORREF C_BTN_DN   = RGB(26, 37, 60);
constexpr COLORREF C_BTN_EDGE = RGB(86, 102, 138);
constexpr COLORREF C_EDIT_BG  = RGB(17, 21, 31);

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

struct Layout {
    RECT header{};
    RECT leftCard{};
    RECT rightCard{};
    RECT logCard{};
};

extern HWND g_hMain;
extern HWND hBtnCheck;
extern HWND hBtnFlash;
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

extern HFONT g_hFontTitle;
extern HFONT g_hFontSub;
extern HFONT g_hFontBody;
extern HFONT g_hFontMono;

extern HBRUSH g_brPanel;
extern HBRUSH g_brPanel2;
extern HBRUSH g_brEdit;
extern HBRUSH g_brTransparent;

extern std::atomic<bool> g_DeviceVerified;
extern std::atomic<bool> g_Busy;
extern std::atomic<bool> g_Unlocked;

extern std::wstring g_StatusText;
extern std::wstring g_DeviceText;
extern std::wstring g_FastbootText;
extern std::wstring g_RomText;
extern std::wstring g_StepsText;
extern std::wstring g_HintText;

extern Layout g_layout;

void SafeDeleteObject(HGDIOBJ obj);
std::wstring ToWide(const std::string& s);
std::wstring Win32ErrorText(DWORD err);
bool FileExistsA(const std::string& path);
void AppendLog(const wchar_t* text);
void PostLog(const std::wstring& msg);
void PostText(UINT kind, const std::wstring& msg);
std::string RomDir();
std::string FASTBOOT_EXE();
std::string FB(const std::string& args);
std::string AssetPath(const char* filename);
std::string Img(const char* filename);
ExecResult Exec(const std::string& cmdLine);
HFONT MakeFont(const wchar_t* face, int size, int weight = FW_NORMAL, BOOL italic = FALSE);
void UpdateText(HWND hwnd, const std::wstring& text);
void UpdateStatusUI(const std::wstring& text);
void UpdateDeviceUI(const std::wstring& text);
void UpdateHintUI(const std::wstring& text);
void UpdateStepsUI(const std::wstring& text);
void LayoutControls(HWND hwnd);
void DrawRoundCard(HDC hdc, const RECT& rc, COLORREF fill, COLORREF edge, int radius = 18);
void DrawChip(HDC hdc, int x, int y, int w, int h, COLORREF fill, COLORREF edge, const wchar_t* text);
void PaintMain(HDC hdc, const RECT& rc);
void PostDone(bool ok, bool flashMode);
void CheckThread();
void FlashThread();
void DrawButtonFace(LPDRAWITEMSTRUCT dis, bool hot, bool pressed, bool enabled);
void CleanupGdi();
std::vector<FlashStep> BuildFlashSteps();
