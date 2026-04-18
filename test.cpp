#include <windows.h>
#include <commctrl.h>

#ifndef EM_SETBKGNDCOLOR
#define EM_SETBKGNDCOLOR 0x0443
#endif
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

constexpr int ID_BTN_CHECK   = 101;
constexpr int ID_BTN_FLASH   = 102;
constexpr int ID_PROGRESS    = 103;
constexpr int ID_LOG_WINDOW  = 104;
constexpr int ID_LBL_STATUS  = 105;
constexpr int ID_LBL_DEVICE  = 106;
constexpr int ID_LBL_FASTBOT = 107;
constexpr int ID_LBL_ROM     = 108;
constexpr int ID_LBL_STEPS   = 109;
constexpr int ID_LBL_HINT    = 110;
constexpr int ID_LBL_TITLE   = 111;
constexpr int ID_LBL_SUB     = 112;

constexpr UINT WM_LOG_POST   = WM_APP + 1;
constexpr UINT WM_PROG_SET   = WM_APP + 2;
constexpr UINT WM_OP_DONE    = WM_APP + 3;
constexpr UINT WM_STATE_SET  = WM_APP + 4;

static const char FASTBOOT[]   = ".\\platform-tools\\fastboot.exe";
static const char ROM_DIR[]    = "TAB-A05-BD";
static const char TARGET_ID[]  = "a05bd";
static const wchar_t APP_CLASS[] = L"A05BDFlasher";

static constexpr COLORREF C_BG        = RGB(15, 18, 28);
static constexpr COLORREF C_BG2       = RGB(21, 26, 40);
static constexpr COLORREF C_PANEL     = RGB(25, 31, 48);
static constexpr COLORREF C_PANEL2    = RGB(30, 38, 58);
static constexpr COLORREF C_LINE      = RGB(56, 69, 98);
static constexpr COLORREF C_TEXT      = RGB(237, 242, 247);
static constexpr COLORREF C_MUTED     = RGB(155, 168, 190);
static constexpr COLORREF C_ACCENT    = RGB(0, 179, 255);
static constexpr COLORREF C_ACCENT2   = RGB(62, 213, 172);
static constexpr COLORREF C_SUCCESS   = RGB(52, 199, 89);
static constexpr COLORREF C_WARNING   = RGB(255, 185, 0);
static constexpr COLORREF C_DANGER    = RGB(255, 92, 92);
static constexpr COLORREF C_BTN       = RGB(39, 49, 72);
static constexpr COLORREF C_BTN_HOVER  = RGB(52, 63, 92);
static constexpr COLORREF C_BTN_DOWN   = RGB(26, 37, 60);
static constexpr COLORREF C_BTN_EDGE   = RGB(86, 102, 138);
static constexpr COLORREF C_EDIT_BG    = RGB(17, 21, 31);

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

std::wstring g_HintText = L"端末を fastboot モードで接続してから「端末確認」を押してください。";

HFONT g_hFontTitle{};
HFONT g_hFontSub{};
HFONT g_hFontBody{};
HFONT g_hFontMono{};

HBRUSH g_brBg{};
HBRUSH g_brPanel{};
HBRUSH g_brPanel2{};
HBRUSH g_brEdit{};
HBRUSH g_brBtn{};
HBRUSH g_brBtnHover{};
HBRUSH g_brBtnDown{};
HBRUSH g_brBtnEdge{};
HBRUSH g_brText{};
HBRUSH g_brTransparent{};

std::atomic<bool> g_DeviceVerified{false};
std::atomic<bool> g_Busy{false};
std::atomic<bool> g_IsFlashMode{false};

struct ExecResult {
    DWORD exitCode;
    std::string output;
};

struct LayoutRect {
    RECT main{};
    RECT header{};
    RECT leftCard{};
    RECT rightCard{};
    RECT logCard{};
    RECT actionCard{};
};

LayoutRect g_layout{};

enum AppState {
    StateIdle,
    StateChecking,
    StateReady,
    StateFlashing,
    StateSuccess,
    StateError
};

std::atomic<AppState> g_State{StateIdle};

std::wstring g_StatusText = L"待機中";
std::wstring g_DeviceText  = L"未確認";
std::wstring g_FastbootText = L".\\platform-tools\\fastboot.exe";
std::wstring g_RomText = L"TAB-A05-BD\\";
std::wstring g_StepsText = L"partition / system / vendor / vbmeta / recovery";

void SafeDeleteObject(HGDIOBJ obj) {
    if (obj) DeleteObject(obj);
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0);
    if (n > 0) {
        std::wstring w(n - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    }
    n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string ToNarrow(const std::wstring& ws) {
    if (ws.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

void PostLog(const std::wstring& msg);

void SetStateText(const std::wstring& text) {
    g_StatusText = text;
    if (hLblStatus) SetWindowTextW(hLblStatus, text.c_str());
}

void SetDeviceText(const std::wstring& text) {
    g_DeviceText = text;
    if (hLblDevice) SetWindowTextW(hLblDevice, text.c_str());
}

void SetStepsText(const std::wstring& text) {
    g_StepsText = text;
    if (hLblSteps) SetWindowTextW(hLblSteps, text.c_str());
}

void SetHintText(const std::wstring& text) {
    g_HintText = text;
    if (hLblHint) SetWindowTextW(hLblHint, text.c_str());
}

ExecResult Exec(const std::string& cmdLine) {
    ExecResult r{1, ""};

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return r;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<char> command(cmdLine.begin(), cmdLine.end());
    command.push_back('\0');

    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return r;
    }

    CloseHandle(hWrite);

    char buf[512];
    DWORD n = 0;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &n, nullptr) && n > 0) {
        buf[n] = '\0';
        r.output += buf;
    }

    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &r.exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return r;
}

std::string FB(const std::string& args) {
    return std::string("\"") + FASTBOOT + "\" " + args;
}

std::string Img(const char* filename) {
    return std::string("\"") + ROM_DIR + "\\" + filename + "\"";
}

void AppendLog(const wchar_t* text) {
    if (!hLog) return;
    int i = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, static_cast<WPARAM>(i), static_cast<LPARAM>(i));
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text));
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"\r\n"));
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}

void PostLog(const std::wstring& msg) {
    auto* p = new wchar_t[msg.size() + 1];
    wmemcpy(p, msg.c_str(), msg.size() + 1);
    PostMessageW(g_hMain, WM_LOG_POST, 0, reinterpret_cast<LPARAM>(p));
}

void UpdateAllStatusControls() {
    if (hLblStatus) SetWindowTextW(hLblStatus, g_StatusText.c_str());
    if (hLblDevice) SetWindowTextW(hLblDevice, g_DeviceText.c_str());
    if (hLblFastboot) SetWindowTextW(hLblFastboot, g_FastbootText.c_str());
    if (hLblRom) SetWindowTextW(hLblRom, g_RomText.c_str());
    if (hLblSteps) SetWindowTextW(hLblSteps, g_StepsText.c_str());
}

void SetAppState(AppState state, const std::wstring& text) {
    g_State = state;
    SetStateText(text);
    InvalidateRect(g_hMain, nullptr, TRUE);
}

void LayoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int margin = 18;
    const int gap = 14;
    const int headerH = 116;
    const int logH = 210;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    int cardTop = margin + headerH;
    int cardHeight = 158;
    int leftW = (width - margin * 2 - gap) / 2;
    int rightW = width - margin * 2 - gap - leftW;

    g_layout.header = { margin, margin, width - margin, margin + headerH - 6 };
    g_layout.leftCard = { margin, cardTop, margin + leftW, cardTop + cardHeight };
    g_layout.rightCard = { margin + leftW + gap, cardTop, margin + leftW + gap + rightW, cardTop + cardHeight };

    int logTop = cardTop + cardHeight + gap;
    g_layout.logCard = { margin, logTop, width - margin, height - margin };

    int cardPad = 18;
    int leftTextX = g_layout.leftCard.left + cardPad;
    int y = g_layout.leftCard.top + 18;

    MoveWindow(hLblStatus, g_layout.rightCard.left + cardPad, g_layout.rightCard.top + 16,
               rightW - cardPad * 2, 26, TRUE);

    MoveWindow(hLblHint, g_layout.rightCard.left + cardPad, g_layout.rightCard.top + 44,
               rightW - cardPad * 2, 28, TRUE);

    MoveWindow(hBtnCheck, g_layout.rightCard.left + cardPad, g_layout.rightCard.top + 86,
               (rightW - cardPad * 2 - 12) / 2, 42, TRUE);
    MoveWindow(hBtnFlash, g_layout.rightCard.left + cardPad + (rightW - cardPad * 2 - 12) / 2 + 12,
               g_layout.rightCard.top + 86, (rightW - cardPad * 2 - 12) / 2, 42, TRUE);

    MoveWindow(hProgressBar, g_layout.rightCard.left + cardPad, g_layout.rightCard.top + 134,
               rightW - cardPad * 2, 14, TRUE);

    MoveWindow(hLblDevice, leftTextX, y + 22, leftW - cardPad * 2, 24, TRUE);
    MoveWindow(hLblFastboot, leftTextX, y + 66, leftW - cardPad * 2, 24, TRUE);
    MoveWindow(hLblRom, leftTextX, y + 110, leftW - cardPad * 2, 24, TRUE);
    MoveWindow(hLblSteps, g_layout.leftCard.left + cardPad, g_layout.leftCard.top + 128, leftW - cardPad * 2, 20, TRUE);

    MoveWindow(hLog, g_layout.logCard.left + 14, g_layout.logCard.top + 42,
               (g_layout.logCard.right - g_layout.logCard.left) - 28,
               (g_layout.logCard.bottom - g_layout.logCard.top) - 56,
               TRUE);
}

void DrawRoundCard(HDC hdc, const RECT& rc, COLORREF fill, COLORREF edge, int radius = 18) {
    HBRUSH br = CreateSolidBrush(fill);
    HGDIOBJ old = SelectObject(hdc, br);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, old);
    DeleteObject(pen);
    DeleteObject(br);
}

void DrawHeaderText(HDC hdc, const RECT& rc) {
    RECT titleRc = rc;
    titleRc.top += 2;
    titleRc.bottom = titleRc.top + 42;
    SelectObject(hdc, g_hFontTitle);
    SetTextColor(hdc, C_TEXT);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, L"a05bd フラッシャー", -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT subRc = rc;
    subRc.top += 44;
    subRc.bottom = subRc.top + 28;
    SelectObject(hdc, g_hFontSub);
    SetTextColor(hdc, C_MUTED);
}

void DrawChip(HDC hdc, int x, int y, int w, int h, COLORREF fill, COLORREF edge, const wchar_t* text) {
    RECT rc{ x, y, x + w, y + h };
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldB = SelectObject(hdc, br);
    HGDIOBJ oldP = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);
    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(br);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, C_TEXT);
    SelectObject(hdc, g_hFontBody);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawPanelTexts(HDC hdc, const RECT& rc, const wchar_t* title, const wchar_t* value, const wchar_t* hint) {
    RECT titleRc = rc;
    titleRc.left += 18;
    titleRc.top += 16;
    titleRc.right -= 18;
    titleRc.bottom = titleRc.top + 22;
    SelectObject(hdc, g_hFontBody);
    SetTextColor(hdc, C_MUTED);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, title, -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT valueRc = rc;
    valueRc.left += 18;
    valueRc.top += 40;
    valueRc.right -= 18;
    valueRc.bottom = valueRc.top + 30;
    SelectObject(hdc, g_hFontBody);
    SetTextColor(hdc, C_TEXT);
    DrawTextW(hdc, value, -1, &valueRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT hintRc = rc;
    hintRc.left += 18;
    hintRc.top += 74;
    hintRc.right -= 18;
    hintRc.bottom -= 16;
    SelectObject(hdc, g_hFontBody);
    SetTextColor(hdc, C_MUTED);
    DrawTextW(hdc, hint, -1, &hintRc, DT_LEFT | DT_WORDBREAK);
}

void DrawMainBackground(HDC hdc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    RECT header = { rc.left, rc.top, rc.right, rc.top + 126 };
    TRIVERTEX v[2]{};
    v[0].x = header.left;
    v[0].y = header.top;
    v[0].Red = GetRValue(C_BG2) << 8;
    v[0].Green = GetGValue(C_BG2) << 8;
    v[0].Blue = GetBValue(C_BG2) << 8;
    v[0].Alpha = 0xff00;
    v[1].x = header.right;
    v[1].y = header.bottom;
    v[1].Red = GetRValue(RGB(24, 31, 50)) << 8;
    v[1].Green = GetGValue(RGB(24, 31, 50)) << 8;
    v[1].Blue = GetBValue(RGB(24, 31, 50)) << 8;
    v[1].Alpha = 0xff00;
    GRADIENT_RECT gRect{ 0, 1 };
    GradientFill(hdc, v, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

    HPEN pen = CreatePen(PS_SOLID, 1, C_LINE);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    DrawRoundCard(hdc, g_layout.leftCard, C_PANEL, C_LINE, 18);
    DrawRoundCard(hdc, g_layout.rightCard, C_PANEL, C_LINE, 18);

    RECT logArea = g_layout.logCard;
    DrawRoundCard(hdc, logArea, C_PANEL2, C_LINE, 18);

    RECT accent = { rc.left + 18, 116, rc.right - 18, 118 };
    HBRUSH accentBr = CreateSolidBrush(C_ACCENT);
    FillRect(hdc, &accent, accentBr);
    DeleteObject(accentBr);

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void CheckThread() {
    g_IsFlashMode = false;
    SetAppState(StateChecking, L"端末確認中");
    SetDeviceText(L"検出待ち");
    PostLog(L"━━ 端末確認 ━━");
    PostLog(L"fastboot デバイスをスキャンしています…");

    auto d = Exec(FB("devices"));
    std::string dl = d.output;
    std::transform(dl.begin(), dl.end(), dl.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (dl.find("fastboot") == std::string::npos) {
        PostLog(L"エラー: fastboot モードの端末が検出されません。");
        PostMessageW(g_hMain, WM_OP_DONE, 0, 0);
        g_Busy = false;
        return;
    }

    PostLog(L"端末を検出しました。product を確認しています…");
    auto v = Exec(FB("getvar product"));
    std::string vl = v.output;
    std::transform(vl.begin(), vl.end(), vl.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (vl.find(TARGET_ID) != std::string::npos) {
        g_DeviceVerified = true;
        SetDeviceText(L"確認済み");
        PostLog(L"確認済み: " + ToWide(TARGET_ID));
        PostMessageW(g_hMain, WM_OP_DONE, 1, 0);
    } else {
        g_DeviceVerified = false;
        SetDeviceText(L"不一致");
        PostLog(L"エラー: モデル不一致。期待値: [" + ToWide(TARGET_ID) + L"]");
        PostLog(L"返答: " + ToWide(v.output));
        PostMessageW(g_hMain, WM_OP_DONE, 0, 0);
    }
    g_Busy = false;
}

void FlashThread() {
    g_IsFlashMode = true;
    SetAppState(StateFlashing, L"書き込み中");
    PostLog(L"━━ 書き込み開始 ━━");

    struct Step { std::string cmd; const wchar_t* desc; };
    const std::vector<Step> steps = {
        { FB("flash partition " + Img("gpt.bin")),      L"1/5  GPT 書き込み中…" },
        { FB("flash system "    + Img("system.img")),   L"2/5  System 書き込み中…" },
        { FB("flash vendor "    + Img("vendor.img")),   L"3/5  Vendor 書き込み中…" },
        { FB("flash vbmeta "    + Img("vbmeta.img")),   L"4/5  VBMeta 書き込み中…" },
        { FB("flash recovery "  + Img("recovery.img")), L"5/5  Recovery 書き込み中…" },
    };

    PostMessageW(g_hMain, WM_PROG_SET,
                 MAKEWPARAM(0, static_cast<WORD>(steps.size())), 0);

    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        PostLog(steps[i].desc);
        auto r = Exec(steps[i].cmd);
        if (r.exitCode != 0) {
            PostLog(L"失敗  終了コード=" + std::to_wstring(r.exitCode));
            if (!r.output.empty()) PostLog(L"出力: " + ToWide(r.output));
            PostMessageW(g_hMain, WM_OP_DONE, 0, 1);
            g_Busy = false;
            return;
        }
        PostMessageW(g_hMain, WM_PROG_SET,
                     MAKEWPARAM(static_cast<WORD>(i + 1), static_cast<WORD>(steps.size())), 0);
    }

    PostLog(L"==============================");
    PostLog(L"すべてのパーティションの書き込みに成功しました。");
    PostMessageW(g_hMain, WM_OP_DONE, 1, 1);
    g_Busy = false;
}

void ApplyButtonFace(HWND hwnd) {
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBody), TRUE);
}

HFONT MakeFont(const wchar_t* face, int size, int weight = FW_NORMAL, BOOL italic = FALSE) {
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    return CreateFontW(
        -MulDiv(size, dpi, 72), 0, 0, 0,
        weight, italic, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face
    );
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hwnd;

        g_hFontTitle = MakeFont(L"Segoe UI Variable", 22, FW_BOLD);
        g_hFontSub   = MakeFont(L"Segoe UI", 10, FW_NORMAL);
        g_hFontBody   = MakeFont(L"Segoe UI", 10, FW_NORMAL);
        g_hFontMono   = MakeFont(L"Consolas", 10, FW_NORMAL);

        g_brBg        = CreateSolidBrush(C_BG);
        g_brPanel     = CreateSolidBrush(C_PANEL);
        g_brPanel2    = CreateSolidBrush(C_PANEL2);
        g_brEdit      = CreateSolidBrush(C_EDIT_BG);
        g_brBtn       = CreateSolidBrush(C_BTN);
        g_brBtnHover  = CreateSolidBrush(C_BTN_HOVER);
        g_brBtnDown   = CreateSolidBrush(C_BTN_DOWN);
        g_brBtnEdge   = CreateSolidBrush(C_BTN_EDGE);
        g_brText      = CreateSolidBrush(C_TEXT);
        g_brTransparent = (HBRUSH)GetStockObject(NULL_BRUSH);

        auto MkCtrl = [&](const wchar_t* cls, const wchar_t* txt, DWORD sty,
                          int x, int y, int w, int h, int id, HFONT font,
                          DWORD ex = 0) -> HWND {
            HWND hw = CreateWindowExW(ex, cls, txt,
                                      WS_VISIBLE | WS_CHILD | sty,
                                      x, y, w, h, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                                      nullptr, nullptr);
            if (font) SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            return hw;
        };

        hLblStatus = MkCtrl(L"STATIC", L"待機中", SS_LEFT, 0, 0, 0, 0, ID_LBL_STATUS, g_hFontBody);
        hLblHint = MkCtrl(L"STATIC", g_HintText.c_str(),
                          SS_LEFT, 0, 0, 0, 0, ID_LBL_HINT, g_hFontBody);
        hLblDevice = MkCtrl(L"STATIC", L"未確認", SS_LEFT, 0, 0, 0, 0, ID_LBL_DEVICE, g_hFontBody);
        hLblFastboot = MkCtrl(L"STATIC", L".\\platform-tools\\fastboot.exe", SS_LEFT, 0, 0, 0, 0, ID_LBL_FASTBOT, g_hFontBody);
        hLblRom = MkCtrl(L"STATIC", L"TAB-A05-BD\\", SS_LEFT, 0, 0, 0, 0, ID_LBL_ROM, g_hFontBody);
        hLblSteps = MkCtrl(L"STATIC", L"partition / system / vendor / vbmeta / recovery",
                           SS_LEFT, 0, 0, 0, 0, ID_LBL_STEPS, g_hFontBody);

        hBtnCheck = MkCtrl(L"BUTTON", L"端末確認",
                           BS_OWNERDRAW | BS_PUSHBUTTON, 0, 0, 0, 0,
                           ID_BTN_CHECK, g_hFontBody);
        hBtnFlash = MkCtrl(L"BUTTON", L"ROM 書き込み",
                           BS_OWNERDRAW | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0,
                           ID_BTN_FLASH, g_hFontBody);

        hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                       WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
                                       0, 0, 0, 0, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_PROGRESS)),
                                       nullptr, nullptr);
        SendMessageW(hProgressBar, PBM_SETMARQUEE, FALSE, 0);

        hLog = MkCtrl(L"EDIT", L"",
                      ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | ES_NOHIDESEL,
                      0, 0, 0, 0, ID_LOG_WINDOW, g_hFontMono, WS_EX_CLIENTEDGE);

        SendMessageW(hLog, EM_SETLIMITTEXT, 0, 0);
        SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, C_EDIT_BG);
        SetWindowTextW(hLog, L"");


        SetAppState(StateIdle, L"待機中");
        SetDeviceText(L"未確認");
        SetStepsText(L"partition / system / vendor / vbmeta / recovery");
        SetHintText(L"端末を fastboot モードで接続してから「端末確認」を押してください。");
        UpdateAllStatusControls();

        AppendLog(L"fastboot    : .\\platform-tools\\fastboot.exe");
        AppendLog(L"ROM フォルダ: TAB-A05-BD\\");
        AppendLog(L"書き込み対象: partition / system / vendor / vbmeta / recovery");
        AppendLog(L"--------------------------------------------------------------");
        AppendLog(L"端末を fastboot モードで接続し、「端末確認」を押してください。");

        LayoutControls(hwnd);
        break;
    }

    case WM_SIZE:
        LayoutControls(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = 760;
        mmi->ptMinTrackSize.y = 560;
        return 0;
    }

    case WM_COMMAND:
        if (g_Busy) break;
        if (LOWORD(wp) == ID_BTN_CHECK) {
            g_DeviceVerified = false;
            g_Busy = true;
            SetDeviceText(L"確認中…");
            SetAppState(StateChecking, L"端末確認中");
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            SendMessageW(hProgressBar, PBM_SETPOS, 0, 0);
            std::thread(CheckThread).detach();
        } else if (LOWORD(wp) == ID_BTN_FLASH && g_DeviceVerified) {
            g_Busy = true;
            SetAppState(StateFlashing, L"書き込み中");
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            std::thread(FlashThread).detach();
        }
        break;

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
        if (dis->CtlType != ODT_BUTTON) break;

        bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;

        RECT rc = dis->rcItem;
        COLORREF fill = enabled ? (pressed ? C_BTN_DOWN : C_BTN) : RGB(33, 39, 54);
        COLORREF edge = enabled ? C_BTN_EDGE : RGB(68, 78, 103);

        HBRUSH br = CreateSolidBrush(fill);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, br);
        HPEN pen = CreatePen(PS_SOLID, 1, edge);
        HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
        SetBkMode(dis->hDC, TRANSPARENT);
        RoundRect(dis->hDC, rc.left, rc.top, rc.right, rc.bottom, 14, 14);
        SelectObject(dis->hDC, oldPen);
        SelectObject(dis->hDC, oldBrush);
        DeleteObject(br);
        DeleteObject(pen);

        wchar_t text[256]{};
        GetWindowTextW(dis->hwndItem, text, static_cast<int>(sizeof(text) / sizeof(text[0])));
        RECT trc = rc;
        SetTextColor(dis->hDC, enabled ? C_TEXT : C_MUTED);
        SelectObject(dis->hDC, g_hFontBody);
        DrawTextW(dis->hDC, text, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        HWND ctrl = reinterpret_cast<HWND>(lp);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, C_TEXT);

        if (ctrl == hLblSub || ctrl == hLblHint || ctrl == hLblSteps) {
            SetTextColor(hdc, C_MUTED);
        } else if (ctrl == hLblStatus) {
            if (g_State == StateReady || g_State == StateSuccess) SetTextColor(hdc, C_SUCCESS);
            else if (g_State == StateChecking || g_State == StateFlashing) SetTextColor(hdc, C_WARNING);
            else if (g_State == StateError) SetTextColor(hdc, C_DANGER);
            else SetTextColor(hdc, C_ACCENT);
        } else if (ctrl == hLblDevice || ctrl == hLblFastboot || ctrl == hLblRom) {
            SetTextColor(hdc, C_TEXT);
        }
        return reinterpret_cast<LRESULT>(g_brTransparent);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkColor(hdc, C_EDIT_BG);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brEdit);
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brTransparent);
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        DrawMainBackground(hdc, rc);
        DrawHeaderText(hdc, g_layout.header);

        int chipW = 118;
        int chipH = 26;
        int top = 24;
        int right = rc.right - 18;
        DrawChip(hdc, right - chipW * 3 - 18 * 2, top, chipW, chipH, RGB(34, 44, 68), C_LINE, L"FASTBOOT");
        DrawChip(hdc, right - chipW * 2 - 18, top, chipW, chipH, RGB(30, 54, 70), RGB(64, 114, 134), L"UTF-8");
        DrawChip(hdc, right - chipW, top, chipW, chipH, RGB(32, 62, 52), RGB(64, 120, 96), L"SAFE UI");

        RECT logTitle = g_layout.logCard;
        logTitle.left += 16;
        logTitle.top += 12;
        logTitle.right -= 16;
        logTitle.bottom = logTitle.top + 24;
        SelectObject(hdc, g_hFontBody);
        SetTextColor(hdc, C_TEXT);
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, L"実行ログ", -1, &logTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        RECT logSub = g_layout.logCard;
        logSub.left += 90;
        logSub.top += 12;
        logSub.right -= 16;
        logSub.bottom = logSub.top + 24;
        SelectObject(hdc, g_hFontBody);
        SetTextColor(hdc, C_MUTED);
        DrawTextW(hdc, L"fastboot の出力と状態遷移を表示します。", -1, &logSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LOG_POST: {
        auto* p = reinterpret_cast<wchar_t*>(lp);
        AppendLog(p);
        delete[] p;
        return 0;
    }

    case WM_PROG_SET: {
        WORD pos = LOWORD(wp);
        WORD rng = HIWORD(wp);
        SendMessageW(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, rng));
        SendMessageW(hProgressBar, PBM_SETPOS, pos, 0);
        return 0;
    }

    case WM_STATE_SET: {
        auto* p = reinterpret_cast<wchar_t*>(lp);
        if (p) {
            SetStateText(p);
            delete[] p;
        }
        return 0;
    }

    case WM_OP_DONE: {
        bool ok = (wp != 0);
        bool isFlash = (lp != 0);

        EnableWindow(hBtnCheck, TRUE);

        if (!isFlash) {
            g_State = ok ? StateReady : StateError;
            EnableWindow(hBtnFlash, ok ? TRUE : FALSE);
            SetStateText(ok ? L"検証完了" : L"検証失敗");

            if (ok) {
                MessageBoxW(hwnd,
                    (L"端末を確認しました: " + ToWide(TARGET_ID)).c_str(),
                    L"確認完了", MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd,
                    L"端末が見つからないか、モデルが一致しません。\n"
                    L"TAB-A05-BD を fastboot モードで接続してください。",
                    L"確認失敗", MB_ICONERROR);
            }
        } else {
            g_DeviceVerified = false;
            g_State = ok ? StateSuccess : StateError;
            EnableWindow(hBtnFlash, FALSE);
            SetStateText(ok ? L"書き込み完了" : L"書き込み失敗");

            if (ok) {
                MessageBoxW(hwnd,
                    L"すべての書き込みに成功しました。",
                    L"書き込み完了", MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd,
                    L"書き込みに失敗しました。ログを確認してください。",
                    L"書き込みエラー", MB_ICONERROR);
            }
        }

        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void CleanupGdi() {
    SafeDeleteObject(g_hFontTitle);
    SafeDeleteObject(g_hFontSub);
    SafeDeleteObject(g_hFontBody);
    SafeDeleteObject(g_hFontMono);

    SafeDeleteObject(g_brBg);
    SafeDeleteObject(g_brPanel);
    SafeDeleteObject(g_brPanel2);
    SafeDeleteObject(g_brEdit);
    SafeDeleteObject(g_brBtn);
    SafeDeleteObject(g_brBtnHover);
    SafeDeleteObject(g_brBtnDown);
    SafeDeleteObject(g_brBtnEdge);
    SafeDeleteObject(g_brText);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = APP_CLASS;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);

    g_hMain = CreateWindowExW(0, APP_CLASS,
                              L"a05bd フラッシャー v1.0",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 900, 700,
                              nullptr, nullptr, hInst, nullptr);

    if (!g_hMain) {
        CleanupGdi();
        return 0;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CleanupGdi();
    return static_cast<int>(msg.wParam);
}
