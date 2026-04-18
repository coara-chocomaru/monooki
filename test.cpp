#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cctype>

#ifndef EM_SETBKGNDCOLOR
#define EM_SETBKGNDCOLOR 0x0443
#endif

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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

static const wchar_t APP_CLASS[] = L"A05BDFlasher";

static constexpr COLORREF C_BG       = RGB(15, 18, 28);
static constexpr COLORREF C_BG2      = RGB(21, 26, 40);
static constexpr COLORREF C_PANEL    = RGB(25, 31, 48);
static constexpr COLORREF C_PANEL2   = RGB(30, 38, 58);
static constexpr COLORREF C_LINE     = RGB(56, 69, 98);
static constexpr COLORREF C_TEXT     = RGB(237, 242, 247);
static constexpr COLORREF C_MUTED    = RGB(155, 168, 190);
static constexpr COLORREF C_ACCENT   = RGB(0, 179, 255);
static constexpr COLORREF C_SUCCESS  = RGB(52, 199, 89);
static constexpr COLORREF C_WARNING  = RGB(255, 185, 0);
static constexpr COLORREF C_DANGER   = RGB(255, 92, 92);
static constexpr COLORREF C_BTN      = RGB(39, 49, 72);
static constexpr COLORREF C_BTN_HOV  = RGB(52, 63, 92);
static constexpr COLORREF C_BTN_DN   = RGB(26, 37, 60);
static constexpr COLORREF C_BTN_EDGE = RGB(86, 102, 138);
static constexpr COLORREF C_EDIT_BG  = RGB(17, 21, 31);

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

std::wstring g_StatusText = L"待機中";
std::wstring g_DeviceText = L"未確認";
std::wstring g_FastbootText = L".\\platform-tools\\fastboot.exe";
std::wstring g_RomText = L"書き込み用フォルダ";
std::wstring g_StepsText = L"wipe / flash / erase / reboot";
std::wstring g_HintText = L"端末を fastboot モードで接続してから「端末確認」を押してください。";

struct ExecResult {
    DWORD exitCode;
    std::string output;
};

struct StepItem {
    std::string cmd;
    std::wstring desc;
};

struct Layout {
    RECT header{};
    RECT leftCard{};
    RECT rightCard{};
    RECT logCard{};
};

Layout g_layout{};

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
    if (n <= 0) return {};
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), n);
    return w;
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

void PostText(UINT kind, const std::wstring& msg) {
    auto* p = new wchar_t[msg.size() + 1];
    wmemcpy(p, msg.c_str(), msg.size() + 1);
    PostMessageW(g_hMain, WM_TEXT_SET, kind, reinterpret_cast<LPARAM>(p));
}

std::string RomDir() {
    std::string a = "TAB";
    std::string b = "-A05-";
    std::string c = "BD";
    return a + b + c;
}

std::string FASTBOOT_EXE() {
    return std::string(".\\platform-tools\\fastboot.exe");
}

std::string FB(const std::string& args) {
    return std::string("\"") + FASTBOOT_EXE() + "\" " + args;
}

std::string Img(const char* filename) {
    return std::string("\"") + RomDir() + "\\" + filename + "\"";
}

ExecResult Exec(const std::string& cmdLine) {
    ExecResult r{ 1, "" };

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
    std::vector<char> cmd(cmdLine.begin(), cmdLine.end());
    cmd.push_back('\0');

    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
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

void UpdateText(HWND hwnd, const std::wstring& text) {
    if (hwnd) SetWindowTextW(hwnd, text.c_str());
}

void UpdateStatusUI(const std::wstring& text) {
    g_StatusText = text;
    UpdateText(hLblStatus, text);
}

void UpdateDeviceUI(const std::wstring& text) {
    g_DeviceText = text;
    UpdateText(hLblDevice, text);
}

void UpdateHintUI(const std::wstring& text) {
    g_HintText = text;
    UpdateText(hLblHint, text);
}

void UpdateStepsUI(const std::wstring& text) {
    g_StepsText = text;
    UpdateText(hLblSteps, text);
}

void LayoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int margin = 18;
    const int gap = 14;
    const int headerH = 110;
    const int cardsH = 164;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    int cardTop = margin + headerH;
    int leftW = (width - margin * 2 - gap) / 2;
    int rightW = width - margin * 2 - gap - leftW;

    g_layout.header = { margin, margin, width - margin, margin + headerH - 8 };
    g_layout.leftCard = { margin, cardTop, margin + leftW, cardTop + cardsH };
    g_layout.rightCard = { margin + leftW + gap, cardTop, margin + leftW + gap + rightW, cardTop + cardsH };
    g_layout.logCard = { margin, cardTop + cardsH + gap, width - margin, height - margin };

    int pad = 18;

    MoveWindow(hLblStatus, g_layout.rightCard.left + pad, g_layout.rightCard.top + 14, rightW - pad * 2, 24, TRUE);
    MoveWindow(hLblHint, g_layout.rightCard.left + pad, g_layout.rightCard.top + 42, rightW - pad * 2, 30, TRUE);
    MoveWindow(hBtnCheck, g_layout.rightCard.left + pad, g_layout.rightCard.top + 86, (rightW - pad * 2 - 12) / 2, 42, TRUE);
    MoveWindow(hBtnFlash, g_layout.rightCard.left + pad + (rightW - pad * 2 - 12) / 2 + 12, g_layout.rightCard.top + 86, (rightW - pad * 2 - 12) / 2, 42, TRUE);
    MoveWindow(hProgressBar, g_layout.rightCard.left + pad, g_layout.rightCard.top + 136, rightW - pad * 2, 16, TRUE);

    MoveWindow(hLblDevice, g_layout.leftCard.left + pad, g_layout.leftCard.top + 38, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblFastboot, g_layout.leftCard.left + pad, g_layout.leftCard.top + 82, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblRom, g_layout.leftCard.left + pad, g_layout.leftCard.top + 126, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblSteps, g_layout.leftCard.left + pad, g_layout.leftCard.top + 146, leftW - pad * 2, 18, TRUE);

    MoveWindow(hLog, g_layout.logCard.left + 14, g_layout.logCard.top + 42, (g_layout.logCard.right - g_layout.logCard.left) - 28, (g_layout.logCard.bottom - g_layout.logCard.top) - 56, TRUE);
}

void DrawRoundCard(HDC hdc, const RECT& rc, COLORREF fill, COLORREF edge, int radius = 18) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(pen);
    DeleteObject(br);
}

void DrawChip(HDC hdc, int x, int y, int w, int h, COLORREF fill, COLORREF edge, const wchar_t* text) {
    RECT rc{ x, y, x + w, y + h };
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(pen);
    DeleteObject(br);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, C_TEXT);
    SelectObject(hdc, g_hFontBody);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PaintMain(HDC hdc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    RECT header = { rc.left, rc.top, rc.right, rc.top + 124 };
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
    GRADIENT_RECT gr{ 0, 1 };
    GradientFill(hdc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);

    DrawRoundCard(hdc, g_layout.leftCard, C_PANEL, C_LINE);
    DrawRoundCard(hdc, g_layout.rightCard, C_PANEL, C_LINE);
    DrawRoundCard(hdc, g_layout.logCard, C_PANEL2, C_LINE);

    RECT accent = { rc.left + 18, 122, rc.right - 18, 124 };
    HBRUSH accentBr = CreateSolidBrush(C_ACCENT);
    FillRect(hdc, &accent, accentBr);
    DeleteObject(accentBr);

    SelectObject(hdc, g_hFontTitle);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, C_TEXT);
    RECT titleRc = g_layout.header;
    titleRc.left += 2;
    titleRc.top += 4;
    titleRc.bottom = titleRc.top + 34;
    DrawTextW(hdc, L"a05bd フラッシャー", -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, g_hFontSub);
    SetTextColor(hdc, C_MUTED);
    RECT subRc = g_layout.header;
    subRc.left += 2;
    subRc.top += 42;
    subRc.bottom = subRc.top + 24;
    DrawTextW(hdc, L"簡易書き込みツール", -1, &subRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    DrawChip(hdc, rc.right - 170, 24, 140, 26, RGB(34, 44, 68), C_LINE, L"fastboot utility");

    RECT logTitle = g_layout.logCard;
    logTitle.left += 16;
    logTitle.top += 12;
    logTitle.right -= 16;
    logTitle.bottom = logTitle.top + 22;
    SelectObject(hdc, g_hFontBody);
    SetTextColor(hdc, C_TEXT);
    DrawTextW(hdc, L"実行ログ", -1, &logTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT logSub = g_layout.logCard;
    logSub.left += 88;
    logSub.top += 12;
    logSub.right -= 16;
    logSub.bottom = logSub.top + 22;
    SetTextColor(hdc, C_MUTED);
    DrawTextW(hdc, L"fastboot の出力と進行状況を表示します。", -1, &logSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

void PostDone(bool ok, bool flashMode) {
    PostMessageW(g_hMain, WM_OP_DONE, ok ? 1 : 0, flashMode ? 1 : 0);
}

void CheckThread() {
    PostText(0, L"端末確認中");
    PostText(1, L"検出中");
    PostText(2, L"端末情報を取得しています。");
    PostLog(L"━━ 端末確認 ━━");
    PostLog(L"fastboot デバイスをスキャンしています…");

    auto d = Exec(FB("devices"));
    std::string dl = d.output;
    std::transform(dl.begin(), dl.end(), dl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (dl.find("fastboot") == std::string::npos) {
        PostLog(L"エラー: fastboot モードの端末が検出されません。");
        PostDone(false, false);
        g_Busy = false;
        return;
    }

    PostLog(L"端末を検出しました。product を確認しています…");
    auto v = Exec(FB("getvar product"));
    std::string vl = v.output;
    std::transform(vl.begin(), vl.end(), vl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (vl.find("a05bd") == std::string::npos) {
        g_DeviceVerified = false;
        PostLog(L"エラー: モデル不一致。期待値: a05bd");
        PostLog(L"返答: " + ToWide(v.output));
        PostDone(false, false);
        g_Busy = false;
        return;
    }

    PostLog(L"端末を確認しました。unlocked を確認しています…");
    auto u = Exec(FB("getvar unlocked"));
    std::string ul = u.output;
    std::transform(ul.begin(), ul.end(), ul.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    bool unlocked = (ul.find("yes") != std::string::npos);
    if (!unlocked) {
        g_DeviceVerified = false;
        g_Unlocked = false;
        PostLog(L"エラー: unlocked が yes ではありません。");
        PostLog(L"返答: " + ToWide(u.output));
        PostLog(L"先にアンロックをしてください！");
        PostDone(false, false);
        g_Busy = false;
        return;
    }

    g_DeviceVerified = true;
    g_Unlocked = true;
    PostLog(L"確認済み: a05bd");
    PostLog(L"unlocked: yes");
    PostDone(true, false);
    g_Busy = false;
}

std::vector<StepItem> BuildFlashSteps() {
    std::vector<StepItem> steps;
    steps.push_back({ FB("-w"), L"0/34  Wipe 実行中…" });
    steps.push_back({ FB("flash nvram " + Img("nvranm.img")), L"1/34  NVRAM 書き込み中…" });
    steps.push_back({ FB("flash nvcfg " + Img("nvcfg.img")), L"2/34  NVCFG 書き込み中…" });
    steps.push_back({ FB("flash nvdata " + Img("nvdata.img")), L"3/34  NVDATA 書き込み中…" });
    steps.push_back({ FB("flash persist " + Img("persist.img")), L"4/34  PERSIST 書き込み中…" });
    steps.push_back({ FB("flash preloader " + Img("preloader.img")), L"5/34  PRELOADER 書き込み中…" });
    steps.push_back({ FB("flash partition " + Img("pgpt.img")), L"6/34  PGPT 書き込み中…" });
    steps.push_back({ FB("flash boot_para " + Img("boot_para.img")), L"7/34  BOOT_PARA 書き込み中…" });
    steps.push_back({ FB("flash cam_vpu1 " + Img("cam_vpu1.img")), L"8/34  CAM_VPU1 書き込み中…" });
    steps.push_back({ FB("flash cam_vpu2 " + Img("cam_vpu2.img")), L"9/34  CAM_VPU2 書き込み中…" });
    steps.push_back({ FB("flash cam_vpu3 " + Img("cam_vpu3.img")), L"10/34  CAM_VPU3 書き込み中…" });
    steps.push_back({ FB("erase protect1"), L"11/34  PROTECT1 消去中…" });
    steps.push_back({ FB("erase protect2"), L"12/34  PROTECT2 消去中…" });
    steps.push_back({ FB("flash nvram " + Img("nvram.img")), L"13/34  NVRAM 書き込み中…" });
    steps.push_back({ FB("flash lk " + Img("lk.img")), L"14/34  LK 書き込み中…" });
    steps.push_back({ FB("flash lk2 " + Img("lk2.img")), L"15/34  LK2 書き込み中…" });
    steps.push_back({ FB("flash boot " + Img("boot.img")), L"16/34  BOOT 書き込み中…" });
    steps.push_back({ FB("flash recovery " + Img("recovery.img")), L"17/34  RECOVERY 書き込み中…" });
    steps.push_back({ FB("flash logo " + Img("logo.img")), L"18/34  LOGO 書き込み中…" });
    steps.push_back({ FB("flash dtbo " + Img("dtbo.img")), L"19/34  DTBO 書き込み中…" });
    steps.push_back({ FB("erase expdb"), L"20/34  EXPDB 消去中…" });
    steps.push_back({ FB("flash frp " + Img("frp.img")), L"21/34  FRP 書き込み中…" });
    steps.push_back({ FB("erase para"), L"22/34  PARA 消去中…" });
    steps.push_back({ FB("flash tee1 " + Img("tee.img")), L"23/34  TEE1 書き込み中…" });
    steps.push_back({ FB("flash tee2 " + Img("tee.img")), L"24/34  TEE2 書き込み中…" });
    steps.push_back({ FB("erase kb"), L"25/34  KB 消去中…" });
    steps.push_back({ FB("erase dkb"), L"26/34  DKB 消去中…" });
    steps.push_back({ FB("erase metadata"), L"27/34  METADATA 消去中…" });
    steps.push_back({ FB("flash vbmeta " + Img("vbmeta.img")), L"28/34  VBMETA 書き込み中…" });
    steps.push_back({ FB("flash system " + Img("system.img")), L"29/34  SYSTEM 書き込み中…" });
    steps.push_back({ FB("flash vendor " + Img("vendor.img")), L"30/34  VENDOR 書き込み中…" });
    steps.push_back({ FB("flash factory " + Img("factory.img")), L"31/34  FACTORY 書き込み中…" });
    steps.push_back({ FB("flash cache " + Img("cache.img")), L"32/34  CACHE 書き込み中…" });
    return steps;
}

void FlashThread() {
    PostText(0, L"書き込み中");
    PostText(2, L"書き込み処理を実行しています。");
    PostLog(L"━━ 書き込み開始 ━━");

    auto steps = BuildFlashSteps();
    PostMessageW(g_hMain, WM_PROG_SET, MAKEWPARAM(0, static_cast<WORD>(steps.size() + 1)), 0);

    for (size_t i = 0; i < steps.size(); ++i) {
        PostLog(steps[i].desc);
        auto r = Exec(steps[i].cmd);
        if (r.exitCode != 0) {
            PostLog(L"失敗  終了コード=" + std::to_wstring(r.exitCode));
            if (!r.output.empty()) PostLog(L"出力: " + ToWide(r.output));
            PostDone(false, true);
            g_Busy = false;
            return;
        }
        PostMessageW(g_hMain, WM_PROG_SET, MAKEWPARAM(static_cast<WORD>(i + 1), static_cast<WORD>(steps.size() + 1)), 0);
    }

    PostLog(L"最終処理: reboot-recovery");
    auto end = Exec(FB("oem reboot-recovery"));
    if (end.exitCode != 0) {
        PostLog(L"失敗  終了コード=" + std::to_wstring(end.exitCode));
        if (!end.output.empty()) PostLog(L"出力: " + ToWide(end.output));
        PostDone(false, true);
        g_Busy = false;
        return;
    }

    PostMessageW(g_hMain, WM_PROG_SET, MAKEWPARAM(static_cast<WORD>(steps.size() + 1), static_cast<WORD>(steps.size() + 1)), 0);
    PostLog(L"==============================");
    PostLog(L"すべての書き込みに成功しました。");
    PostDone(true, true);
    g_Busy = false;
}

void DrawButtonFace(LPDRAWITEMSTRUCT dis, bool hot, bool pressed, bool enabled) {
    RECT rc = dis->rcItem;
    COLORREF fill = enabled ? (pressed ? C_BTN_DN : (hot ? C_BTN_HOV : C_BTN)) : RGB(33, 39, 54);
    COLORREF edge = enabled ? C_BTN_EDGE : RGB(68, 78, 103);

    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldBr = SelectObject(dis->hDC, br);
    HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
    SetBkMode(dis->hDC, TRANSPARENT);
    RoundRect(dis->hDC, rc.left, rc.top, rc.right, rc.bottom, 14, 14);
    SelectObject(dis->hDC, oldPen);
    SelectObject(dis->hDC, oldBr);
    DeleteObject(br);
    DeleteObject(pen);

    wchar_t text[256]{};
    GetWindowTextW(dis->hwndItem, text, static_cast<int>(sizeof(text) / sizeof(text[0])));
    RECT trc = rc;
    SetTextColor(dis->hDC, enabled ? C_TEXT : C_MUTED);
    SelectObject(dis->hDC, g_hFontBody);
    DrawTextW(dis->hDC, text, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void CleanupGdi() {
    SafeDeleteObject(g_hFontTitle);
    SafeDeleteObject(g_hFontSub);
    SafeDeleteObject(g_hFontBody);
    SafeDeleteObject(g_hFontMono);
    SafeDeleteObject(g_brPanel);
    SafeDeleteObject(g_brPanel2);
    SafeDeleteObject(g_brEdit);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hwnd;

        g_hFontTitle = MakeFont(L"Segoe UI", 22, FW_BOLD);
        g_hFontSub   = MakeFont(L"Segoe UI", 10, FW_NORMAL);
        g_hFontBody  = MakeFont(L"Segoe UI", 10, FW_NORMAL);
        g_hFontMono  = MakeFont(L"Consolas", 10, FW_NORMAL);

        g_brPanel = CreateSolidBrush(C_PANEL);
        g_brPanel2 = CreateSolidBrush(C_PANEL2);
        g_brEdit = CreateSolidBrush(C_EDIT_BG);
        g_brTransparent = (HBRUSH)GetStockObject(NULL_BRUSH);

        auto MkCtrl = [&](const wchar_t* cls, const wchar_t* txt, DWORD sty, DWORD ex, int x, int y, int w, int h, int id, HFONT font) -> HWND {
            HWND c = CreateWindowExW(ex, cls, txt, WS_VISIBLE | WS_CHILD | sty,
                                     x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                                     nullptr, nullptr);
            if (c && font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            return c;
        };

        hLblTitle = MkCtrl(L"STATIC", L"a05bd フラッシャー", 0, 0, 0, 0, 0, 0, 0, ID_LBL_TITLE, g_hFontTitle);
        hLblSub   = MkCtrl(L"STATIC", L"簡易書き込みツール", 0, 0, 0, 0, 0, 0, 0, ID_LBL_SUB, g_hFontSub);
        hLblStatus = MkCtrl(L"STATIC", L"待機中", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_STATUS, g_hFontBody);
        hLblHint   = MkCtrl(L"STATIC", g_HintText.c_str(), SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_HINT, g_hFontBody);
        hLblDevice = MkCtrl(L"STATIC", L"未確認", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_DEVICE, g_hFontBody);
        hLblFastboot = MkCtrl(L"STATIC", L".\\platform-tools\\fastboot.exe", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_FASTBT, g_hFontBody);
        hLblRom = MkCtrl(L"STATIC", L"書き込み用フォルダ", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_ROM, g_hFontBody);
        hLblSteps = MkCtrl(L"STATIC", L"wipe / flash / erase / reboot", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_STEPS, g_hFontBody);

        hBtnCheck = MkCtrl(L"BUTTON", L"端末確認", BS_OWNERDRAW | BS_PUSHBUTTON, 0, 0, 0, 0, ID_BTN_CHECK, g_hFontBody);
        hBtnFlash = MkCtrl(L"BUTTON", L"ROM 書き込み", BS_OWNERDRAW | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, ID_BTN_FLASH, g_hFontBody);

        hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                       WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
                                       0, 0, 0, 0, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_PROGRESS)),
                                       nullptr, nullptr);
        SendMessageW(hProgressBar, PBM_SETMARQUEE, FALSE, 0);

        hLog = MkCtrl(L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | ES_NOHIDESEL,
                      WS_EX_CLIENTEDGE, 0, 0, 0, 0, ID_LOG_WINDOW, g_hFontMono);

        SendMessageW(hLog, EM_SETLIMITTEXT, 0, 0);
        SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, C_EDIT_BG);

        UpdateStatusUI(L"待機中");
        UpdateDeviceUI(L"未確認");
        UpdateHintUI(L"端末を fastboot モードで接続してから「端末確認」を押してください。");
        UpdateStepsUI(L"wipe / flash / erase / reboot");

        AppendLog(L"fastboot    : .\\platform-tools\\fastboot.exe");
        AppendLog(L"書き込み元 : 内部定義");
        AppendLog(L"対象処理   : wipe / flash / erase / reboot");
        AppendLog(L"--------------------------------------------------------------");
        AppendLog(L"端末を fastboot モードで接続し、「端末確認」を押してください。");

        LayoutControls(hwnd);
        return 0;
    }

    case WM_SIZE:
        LayoutControls(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = 780;
        mmi->ptMinTrackSize.y = 580;
        return 0;
    }

    case WM_COMMAND:
        if (g_Busy) break;
        if (LOWORD(wp) == ID_BTN_CHECK) {
            g_DeviceVerified = false;
            g_Unlocked = false;
            g_Busy = true;
            UpdateStatusUI(L"端末確認中");
            UpdateDeviceUI(L"確認中…");
            UpdateHintUI(L"端末情報を取得しています。");
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            SendMessageW(hProgressBar, PBM_SETPOS, 0, 0);
            std::thread(CheckThread).detach();
        } else if (LOWORD(wp) == ID_BTN_FLASH && g_DeviceVerified && g_Unlocked) {
            g_Busy = true;
            UpdateStatusUI(L"書き込み中");
            UpdateHintUI(L"書き込み処理を実行しています。");
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            std::thread(FlashThread).detach();
        }
        return 0;

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
        if (dis->CtlType != ODT_BUTTON) break;
        bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
        DrawButtonFace(dis, hot, pressed, enabled);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        HWND ctrl = reinterpret_cast<HWND>(lp);
        SetBkMode(hdc, TRANSPARENT);

        if (ctrl == hLblStatus) {
            if (g_StatusText.find(L"書き込み中") != std::wstring::npos) SetTextColor(hdc, C_WARNING);
            else if (g_StatusText.find(L"失敗") != std::wstring::npos) SetTextColor(hdc, C_DANGER);
            else if (g_StatusText.find(L"完了") != std::wstring::npos || g_StatusText.find(L"確認済み") != std::wstring::npos) SetTextColor(hdc, C_SUCCESS);
            else SetTextColor(hdc, C_ACCENT);
        } else if (ctrl == hLblHint || ctrl == hLblSteps) {
            SetTextColor(hdc, C_MUTED);
        } else if (ctrl == hLblDevice || ctrl == hLblFastboot || ctrl == hLblRom) {
            SetTextColor(hdc, C_TEXT);
        } else {
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
        PaintMain(hdc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LOG_POST: {
        auto* p = reinterpret_cast<wchar_t*>(lp);
        AppendLog(p);
        delete[] p;
        return 0;
    }

    case WM_TEXT_SET: {
        auto* p = reinterpret_cast<wchar_t*>(lp);
        if (!p) return 0;
        if (wp == 0) UpdateStatusUI(p);
        else if (wp == 1) UpdateDeviceUI(p);
        else if (wp == 2) UpdateHintUI(p);
        else if (wp == 3) UpdateStepsUI(p);
        delete[] p;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_PROG_SET: {
        WORD pos = LOWORD(wp);
        WORD rng = HIWORD(wp);
        SendMessageW(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, rng));
        SendMessageW(hProgressBar, PBM_SETPOS, pos, 0);
        return 0;
    }

    case WM_OP_DONE: {
        bool ok = (wp != 0);
        bool flashMode = (lp != 0);

        EnableWindow(hBtnCheck, TRUE);

        if (!flashMode) {
            g_DeviceVerified = ok;
            if (ok && g_Unlocked) {
                EnableWindow(hBtnFlash, TRUE);
                UpdateStatusUI(L"検証完了");
                UpdateDeviceUI(L"確認済み");
                UpdateHintUI(L"書き込みを開始できます。");
                MessageBoxW(hwnd, L"端末を確認しました。unlocked も確認済みです。", L"確認完了", MB_ICONINFORMATION);
            } else {
                EnableWindow(hBtnFlash, FALSE);
                UpdateStatusUI(L"検証失敗");
                UpdateHintUI(L"端末が見つからないか、条件を満たしていません。");
                if (!g_Unlocked) {
                    MessageBoxW(hwnd, L"先にアンロックをしてください！", L"アンロックが必要です", MB_ICONWARNING);
                } else {
                    MessageBoxW(hwnd,
                        L"端末が見つからないか、モデルが一致しません。\n"
                        L"fastboot モードで接続してください。",
                        L"確認失敗", MB_ICONERROR);
                }
            }
        } else {
            g_DeviceVerified = false;
            g_Unlocked = false;
            EnableWindow(hBtnFlash, FALSE);
            if (ok) {
                UpdateStatusUI(L"書き込み完了");
                UpdateHintUI(L"処理が完了しました。");
                MessageBoxW(hwnd, L"すべての書き込みに成功しました。", L"書き込み完了", MB_ICONINFORMATION);
            } else {
                UpdateStatusUI(L"書き込み失敗");
                UpdateHintUI(L"ログを確認してください。");
                MessageBoxW(hwnd, L"書き込みに失敗しました。ログを確認してください。", L"書き込みエラー", MB_ICONERROR);
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

    g_hMain = CreateWindowExW(0, APP_CLASS, L"a05bd フラッシャー v1.0",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 920, 720,
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
