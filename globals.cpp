#include "app.h"

const wchar_t APP_CLASS[] = L"A05BDFlasher";

COLORREF C_BG = RGB(15, 18, 28);
COLORREF C_BG2 = RGB(21, 26, 40);
COLORREF C_PANEL = RGB(25, 31, 48);
COLORREF C_PANEL2 = RGB(30, 38, 58);
COLORREF C_LINE = RGB(56, 69, 98);
COLORREF C_TEXT = RGB(237, 242, 247);
COLORREF C_MUTED = RGB(155, 168, 190);
COLORREF C_ACCENT = RGB(0, 179, 255);
COLORREF C_SUCCESS = RGB(52, 199, 89);
COLORREF C_WARNING = RGB(255, 185, 0);
COLORREF C_DANGER = RGB(255, 92, 92);
COLORREF C_BTN = RGB(39, 49, 72);
COLORREF C_BTN_HOV = RGB(52, 63, 92);
COLORREF C_BTN_DN = RGB(26, 37, 60);
COLORREF C_BTN_EDGE = RGB(86, 102, 138);
COLORREF C_EDIT_BG = RGB(17, 21, 31);

HWND g_hMain{};
HWND hBtnCheck{};
HWND hBtnFlash{};
HWND hBtnSettings{};
HWND hProgressBar{};
HWND hLog{};
HWND hLblStatus{};
HWND hLblDevice{};
HWND hLblFastboot{};
HWND hLblRom{};
HWND hLblSteps{};
HWND hLblHint{};
HWND hLblTitle{};
HWND hLblSub{};

HFONT g_hFontTitle{};
HFONT g_hFontSub{};
HFONT g_hFontBody{};
HFONT g_hFontMono{};

HBRUSH g_brPanel{};
HBRUSH g_brPanel2{};
HBRUSH g_brEdit{};
HBRUSH g_brTransparent{};

std::atomic<bool> g_DeviceVerified{false};
std::atomic<bool> g_Busy{false};
std::atomic<bool> g_Unlocked{false};
std::atomic<uint32_t> g_CurrentOperationToken{0};

std::wstring g_StatusText = L"待機中";
std::wstring g_DeviceText = L"未確認";
std::wstring g_FastbootText = L".\\platform-tools\\fastboot.exe";
std::wstring g_RomText = L"./TAB-A05-BD";
std::wstring g_StepsText = L"wipe / flash / erase / reboot";
std::wstring g_HintText = L"端末を fastboot モードで接続してから「端末確認」を押してください。";
std::wstring g_ConfigThemeKey = L"a";
std::wstring g_ConfigRomDir = L"./TAB-A05-BD";
std::wstring g_ConfigBackgroundImage = L"";

Layout g_layout{};
std::mutex g_LogMutex;
std::deque<LogLine> g_LogQueue;
std::atomic<bool> g_LogFlushPending{false};
