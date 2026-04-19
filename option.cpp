#include "app.h"

#include <commdlg.h>
#include <cwctype>
#include <iterator>

#pragma comment(lib, "comdlg32.lib")

namespace {
constexpr int IDC_THEME = 2001;
constexpr int IDC_IMGDIR = 2002;
constexpr int IDC_BGIMG = 2003;
constexpr int IDC_SAVE = 2004;
constexpr int IDC_RESET = 2005;
constexpr int IDC_CLOSE = 2006;
constexpr int IDC_BG_BROWSE = 2007;

struct SettingsState {
    HWND owner{};
    HWND comboTheme{};
    HWND editImgDir{};
    HWND editBgImg{};
};

std::wstring TrimCopy(const std::wstring& s) {
    size_t begin = 0;
    while (begin < s.size() && iswspace(s[begin])) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && iswspace(s[end - 1])) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::wstring NormalizeInputPath(const std::wstring& raw) {
    std::wstring s = TrimCopy(raw);
    while (!s.empty() && (s.front() == L'"' || s.front() == L'\'')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == L'"' || s.back() == L'\'')) {
        s.pop_back();
    }
    s = TrimCopy(s);
    if (s.empty()) {
        return {};
    }
    DWORD need = GetFullPathNameW(s.c_str(), 0, nullptr, nullptr);
    if (need == 0) {
        return s;
    }
    std::wstring out(static_cast<size_t>(need), L'\0');
    DWORD got = GetFullPathNameW(s.c_str(), need, out.data(), nullptr);
    if (got == 0) {
        return s;
    }
    out.resize(wcslen(out.c_str()));
    return out;
}

std::wstring ReadControlText(HWND hwnd) {
    if (!hwnd) {
        return {};
    }
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) {
        return {};
    }
    std::wstring text(static_cast<size_t>(len + 1), L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(wcslen(text.c_str()));
    return text;
}

ThemePalette ThemeFromIndex(int index) {
    switch (index) {
    case 1:
        return ThemePalette{
            RGB(12, 23, 21), RGB(18, 33, 29), RGB(19, 43, 37), RGB(24, 51, 45), RGB(62, 84, 77),
            RGB(241, 246, 243), RGB(163, 180, 171), RGB(58, 202, 176), RGB(72, 204, 120), RGB(248, 193, 76),
            RGB(245, 104, 104), RGB(33, 55, 48), RGB(44, 73, 63), RGB(24, 41, 35), RGB(77, 102, 92),
            RGB(17, 28, 25), RGB(31, 77, 67), RGB(74, 124, 110)
        };
    case 2:
        return ThemePalette{
            RGB(20, 16, 28), RGB(28, 21, 40), RGB(35, 27, 54), RGB(43, 34, 64), RGB(78, 66, 108),
            RGB(243, 239, 248), RGB(173, 162, 196), RGB(188, 124, 255), RGB(107, 219, 156), RGB(255, 197, 102),
            RGB(255, 117, 117), RGB(48, 39, 73), RGB(64, 51, 93), RGB(36, 29, 56), RGB(102, 88, 145),
            RGB(23, 18, 34), RGB(54, 40, 85), RGB(103, 82, 160)
        };
    default:
        return ThemePalette{
            RGB(15, 18, 28), RGB(21, 26, 40), RGB(25, 31, 48), RGB(30, 38, 58), RGB(56, 69, 98),
            RGB(237, 242, 247), RGB(155, 168, 190), RGB(0, 179, 255), RGB(52, 199, 89), RGB(255, 185, 0),
            RGB(255, 92, 92), RGB(39, 49, 72), RGB(52, 63, 92), RGB(26, 37, 60), RGB(86, 102, 138),
            RGB(17, 21, 31), RGB(34, 44, 68), RGB(86, 102, 138)
        };
    }
}

std::wstring ThemeLabelFromIndex(int index) {
    switch (index) {
    case 1: return L"b";
    case 2: return L"c";
    default: return L"a";
    }
}

int ThemeIndexFromLabel(const std::wstring& text) {
    if (!text.empty()) {
        const wchar_t ch = static_cast<wchar_t>(towlower(text[0]));
        if (ch == L'b') return 1;
        if (ch == L'c') return 2;
    }
    return 0;
}

std::wstring DisplayImageDir() {
    if (g_Config.imageDir.empty()) {
        return L"./TAB-A05-BD";
    }
    return g_Config.imageDir;
}

std::wstring DisplayBgImage() {
    if (g_Config.backgroundImage.empty()) {
        return L"未設定";
    }
    return g_Config.backgroundImage;
}

void RebuildThemeResources() {
    SafeDeleteObject(g_brPanel);
    SafeDeleteObject(g_brPanel2);
    SafeDeleteObject(g_brEdit);
    g_brPanel = CreateSolidBrush(C_PANEL);
    g_brPanel2 = CreateSolidBrush(C_PANEL2);
    g_brEdit = CreateSolidBrush(C_EDIT_BG);
    if (hProgressBar) {
        SendMessageW(hProgressBar, PBM_SETBKCOLOR, 0, static_cast<LPARAM>(C_PANEL));
        SendMessageW(hProgressBar, PBM_SETBARCOLOR, 0, static_cast<LPARAM>(C_ACCENT));
    }
}

void RefreshTexts() {
    g_FastbootText = L"fastboot.exe : .\\platform-tools\\fastboot.exe";
    g_RomText = L"画像フォルダ : " + DisplayImageDir();
    g_BgImageText = L"背景画像 : " + DisplayBgImage();
    g_StepsText = L"手順 : flash / erase / reboot";
    UpdateText(hLblFastboot, g_FastbootText);
    UpdateText(hLblRom, g_RomText);
    UpdateBgImageUI(g_BgImageText);
    UpdateStepsUI(g_StepsText);
    UpdateText(hLblTitle, L"a05bd フラッシャー");
    UpdateText(hLblSub, L"簡易書き込みツール");
}

AppConfig DefaultAppConfig();

bool WriteConfigFile(const AppConfig& cfg) {
    const std::wstring path = ConfigPathW();
    const wchar_t* section = L"Config";
    const std::wstring theme = ThemeLabelFromIndex(cfg.themeIndex);
    if (!WritePrivateProfileStringW(section, L"layout", theme.c_str(), path.c_str())) return false;
    if (!WritePrivateProfileStringW(section, L"img_dir", cfg.imageDir.c_str(), path.c_str())) return false;
    if (!WritePrivateProfileStringW(section, L"background", cfg.backgroundImage.c_str(), path.c_str())) return false;
    return true;
}

AppConfig ReadConfigFromFile() {
    AppConfig cfg = DefaultAppConfig();
    const std::wstring path = ConfigPathW();
    if (!FileExistsW(path)) {
        WriteConfigFile(cfg);
        return cfg;
    }

    wchar_t buf[4096]{};
    GetPrivateProfileStringW(L"Config", L"layout", L"a", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
    cfg.themeIndex = ThemeIndexFromLabel(buf);

    buf[0] = 0;
    GetPrivateProfileStringW(L"Config", L"img_dir", L"", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
    cfg.imageDir = NormalizeInputPath(buf);

    buf[0] = 0;
    GetPrivateProfileStringW(L"Config", L"background", L"", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
    cfg.backgroundImage = NormalizeInputPath(buf);

    return cfg;
}

void FillSettingsControls(SettingsState* state) {
    if (!state) {
        return;
    }
    SendMessageW(state->comboTheme, CB_RESETCONTENT, 0, 0);
    SendMessageW(state->comboTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"a : default"));
    SendMessageW(state->comboTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"b"));
    SendMessageW(state->comboTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"c"));
    SendMessageW(state->comboTheme, CB_SETCURSEL, static_cast<WPARAM>(g_Config.themeIndex), 0);
    SetWindowTextW(state->editImgDir, g_Config.imageDir.empty() ? L"" : g_Config.imageDir.c_str());
    SetWindowTextW(state->editBgImg, g_Config.backgroundImage.empty() ? L"" : g_Config.backgroundImage.c_str());
}

void ApplyThemeToWindow(HWND hwnd) {
    if (hwnd) {
        InvalidateRect(hwnd, nullptr, TRUE);
        UpdateWindow(hwnd);
    }
}

void SaveFromSettings(SettingsState* state) {
    if (!state) {
        return;
    }
    AppConfig cfg{};
    cfg.themeIndex = static_cast<int>(SendMessageW(state->comboTheme, CB_GETCURSEL, 0, 0));
    if (cfg.themeIndex < 0 || cfg.themeIndex > 2) {
        cfg.themeIndex = 0;
    }
    cfg.imageDir = NormalizeInputPath(ReadControlText(state->editImgDir));
    cfg.backgroundImage = NormalizeInputPath(ReadControlText(state->editBgImg));
    SaveAppConfig(cfg);
    ApplyAppConfig(cfg);
}

void BrowseBackgroundImage(HWND owner, HWND targetEdit) {
    wchar_t fileBuf[MAX_PATH * 4]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
    ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0All Files\0*.*\0\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(targetEdit, ofn.lpstrFile);
    }
}

void PaintSettingsWindow(HWND hwnd, HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    Gdiplus::SolidBrush bg(Gdiplus::Color(255, GetRValue(C_BG), GetGValue(C_BG), GetBValue(C_BG)));
    g.FillRectangle(&bg, static_cast<float>(rc.left), static_cast<float>(rc.top),
                    static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top));

    Gdiplus::SolidBrush head(Gdiplus::Color(255, GetRValue(C_BG2), GetGValue(C_BG2), GetBValue(C_BG2)));
    g.FillRectangle(&head, static_cast<float>(rc.left), static_cast<float>(rc.top),
                    static_cast<float>(rc.right - rc.left), 64.0f);

    Gdiplus::SolidBrush accent(Gdiplus::Color(255, GetRValue(C_ACCENT), GetGValue(C_ACCENT), GetBValue(C_ACCENT)));
    g.FillRectangle(&accent, static_cast<float>(rc.left), 64.0f, static_cast<float>(rc.right - rc.left), 2.0f);

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_hFontTitle);
    SetTextColor(hdc, C_TEXT);
    RECT titleRc{18, 14, rc.right - 18, 42};
    DrawTextW(hdc, L"設定", -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, g_hFontBody);
    SetTextColor(hdc, C_MUTED);
    RECT hintRc{18, 40, rc.right - 18, 60};
    DrawTextW(hdc, L"config.ini に保存され、次回起動時も同じディレクトリから読み込まれます。", -1, &hintRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        state = new SettingsState{};
        state->owner = cs ? cs->hwndParent : nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        const int left = 18;
        const int top = 78;
        const int labelW = 142;
        const int editW = 360;
        const int rowH = 28;
        const int gapY = 16;
        const int buttonW = 94;
        const int buttonH = 30;

        CreateWindowExW(0, L"STATIC", L"レイアウト色", WS_CHILD | WS_VISIBLE, left, top + 4, labelW, 20, hwnd, nullptr, nullptr, nullptr);
        state->comboTheme = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                                            left + labelW, top, editW, 200, hwnd, reinterpret_cast<HMENU>(IDC_THEME), nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"画像フォルダ (フルパス)", WS_CHILD | WS_VISIBLE, left, top + rowH + gapY + 4, labelW + 40, 20, hwnd, nullptr, nullptr, nullptr);
        state->editImgDir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                                            left + labelW, top + rowH + gapY, editW, rowH, hwnd, reinterpret_cast<HMENU>(IDC_IMGDIR), nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"背景画像 (フルパス)", WS_CHILD | WS_VISIBLE, left, top + (rowH + gapY) * 2 + 4, labelW + 40, 20, hwnd, nullptr, nullptr, nullptr);
        state->editBgImg = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                                           left + labelW, top + (rowH + gapY) * 2, editW - buttonW - 8, rowH, hwnd, reinterpret_cast<HMENU>(IDC_BGIMG), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"参照...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        left + labelW + editW - buttonW, top + (rowH + gapY) * 2, buttonW, buttonH,
                        hwnd, reinterpret_cast<HMENU>(IDC_BG_BROWSE), nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"保存すると config.ini に反映されます。", WS_CHILD | WS_VISIBLE,
                        left, top + (rowH + gapY) * 3 + 4, 520, 20, hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"背景画像は透過つきで自動リサイズ表示されます。", WS_CHILD | WS_VISIBLE,
                        left, top + (rowH + gapY) * 3 + 26, 520, 20, hwnd, nullptr, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"初期化", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        left, top + (rowH + gapY) * 4 + 28, buttonW, buttonH, hwnd, reinterpret_cast<HMENU>(IDC_RESET), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        left + buttonW + 10, top + (rowH + gapY) * 4 + 28, buttonW, buttonH, hwnd, reinterpret_cast<HMENU>(IDC_SAVE), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"閉じる", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        left + (buttonW + 10) * 2, top + (rowH + gapY) * 4 + 28, buttonW, buttonH, hwnd, reinterpret_cast<HMENU>(IDC_CLOSE), nullptr, nullptr);

        FillSettingsControls(state);

        for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBody), TRUE);
        }
        if (state->comboTheme) {
            SendMessageW(state->comboTheme, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBody), TRUE);
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brTransparent ? g_brTransparent : GetStockObject(NULL_BRUSH));
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, C_PANEL2);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brPanel2 ? g_brPanel2 : g_brTransparent);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, C_EDIT_BG);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brEdit);
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintSettingsWindow(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
        if (dis && dis->CtlType == ODT_BUTTON) {
            const bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
            const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
            DrawButtonFace(dis, hot, pressed, enabled);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        switch (LOWORD(wp)) {
        case IDC_BG_BROWSE:
            BrowseBackgroundImage(hwnd, state ? state->editBgImg : nullptr);
            return 0;
        case IDC_SAVE:
            SaveFromSettings(state);
            ApplyThemeToWindow(g_hMain);
            return 0;
        case IDC_RESET: {
            AppConfig cfg = DefaultAppConfig();
            SaveAppConfig(cfg);
            ApplyAppConfig(cfg);
            FillSettingsControls(state);
            ApplyThemeToWindow(g_hMain);
            return 0;
        }
        case IDC_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case IDC_THEME:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                return 0;
            }
            break;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
        HWND owner = state ? state->owner : nullptr;
        if (state) {
            delete state;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        g_hSettings = nullptr;
        if (owner) {
            EnableWindow(owner, TRUE);
            SetForegroundWindow(owner);
        }
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace


AppConfig DefaultAppConfig() {
    AppConfig cfg{};
    cfg.themeIndex = 0;
    cfg.imageDir.clear();
    cfg.backgroundImage.clear();
    return cfg;
}

AppConfig LoadAppConfig() {
    return ReadConfigFromFile();
}

void SaveAppConfig(const AppConfig& cfg) {
    AppConfig normalized = cfg;
    if (normalized.themeIndex < 0 || normalized.themeIndex > 2) {
        normalized.themeIndex = 0;
    }
    normalized.imageDir = NormalizeInputPath(normalized.imageDir);
    normalized.backgroundImage = NormalizeInputPath(normalized.backgroundImage);
    WriteConfigFile(normalized);
}

void ApplyAppConfig(const AppConfig& cfg) {
    g_Config = cfg;
    if (g_Config.themeIndex < 0 || g_Config.themeIndex > 2) {
        g_Config.themeIndex = 0;
    }
    g_theme = ThemeFromIndex(g_Config.themeIndex);
    RebuildThemeResources();
    RefreshTexts();
    ReloadBackgroundImage();
    if (hProgressBar) {
        SendMessageW(hProgressBar, PBM_SETBKCOLOR, 0, static_cast<LPARAM>(C_PANEL));
        SendMessageW(hProgressBar, PBM_SETBARCOLOR, 0, static_cast<LPARAM>(C_ACCENT));
    }
    if (g_hMain) {
        InvalidateRect(g_hMain, nullptr, TRUE);
    }
    if (hLog) {
        InvalidateRect(hLog, nullptr, TRUE);
    }
}

void OpenSettingsWindow(HWND owner) {
    if (g_hSettings && IsWindow(g_hSettings)) {
        SetForegroundWindow(g_hSettings);
        return;
    }

    if (!owner) {
        owner = g_hMain;
    }

    const wchar_t clsName[] = L"A05BDSettingsWindow";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = clsName;
        wc.hbrBackground = nullptr;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.style = CS_DBLCLKS;
        RegisterClassW(&wc);
        registered = true;
    }

    g_hSettings = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                                  clsName, L"設定", WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 620, 390,
                                  owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_hSettings) {
        return;
    }

    RECT rcOwner{};
    if (owner && GetWindowRect(owner, &rcOwner)) {
        const int w = 620;
        const int h = 390;
        const int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - w) / 2;
        const int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - h) / 2;
        SetWindowPos(g_hSettings, nullptr, x, y, w, h, SWP_NOZORDER);
    }

    if (owner) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(g_hSettings, SW_SHOW);
    UpdateWindow(g_hSettings);

    MSG msg{};
    while (IsWindow(g_hSettings)) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(msg.wParam));
                if (owner) {
                    EnableWindow(owner, TRUE);
                }
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (IsWindow(g_hSettings)) {
            WaitMessage();
        }
    }

    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}
