#include "app.h"

const wchar_t APP_CLASS[] = L"A05BDFlasher";

HWND g_hMain{};
HWND hBtnCheck{};
HWND hBtnFlash{};
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
std::wstring g_RomText = L"TAB-A05-BD";
std::wstring g_StepsText = L"wipe / flash / erase / reboot";
std::wstring g_HintText = L"端末を fastboot モードで接続してから「端末確認」を押してください。";

Layout g_layout{};
std::mutex g_LogMutex;
std::deque<LogLine> g_LogQueue;
std::atomic<bool> g_LogFlushPending{false};
