#include "app.h"

#include <algorithm>
#include <cwctype>
#include <memory>

#include <shlobj.h>
#include <shobjidl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace {

struct ThemePalette {
    COLORREF bg;
    COLORREF bg2;
    COLORREF panel;
    COLORREF panel2;
    COLORREF line;
    COLORREF text;
    COLORREF muted;
    COLORREF accent;
    COLORREF success;
    COLORREF warning;
    COLORREF danger;
    COLORREF btn;
    COLORREF btnHov;
    COLORREF btnDn;
    COLORREF btnEdge;
    COLORREF editBg;
};

constexpr ThemePalette kThemes[] = {
    {RGB(15, 18, 28), RGB(21, 26, 40), RGB(25, 31, 48), RGB(30, 38, 58), RGB(56, 69, 98), RGB(237, 242, 247), RGB(155, 168, 190), RGB(0, 179, 255), RGB(52, 199, 89), RGB(255, 185, 0), RGB(255, 92, 92), RGB(39, 49, 72), RGB(52, 63, 92), RGB(26, 37, 60), RGB(86, 102, 138), RGB(17, 21, 31)},
    {RGB(17, 20, 26), RGB(25, 31, 39), RGB(30, 37, 48), RGB(35, 44, 58), RGB(72, 87, 110), RGB(245, 247, 250), RGB(176, 184, 196), RGB(91, 179, 255), RGB(82, 196, 128), RGB(245, 187, 66), RGB(255, 111, 105), RGB(44, 54, 66), RGB(58, 71, 86), RGB(30, 40, 52), RGB(98, 114, 136), RGB(19, 24, 31)},
    {RGB(14, 20, 18), RGB(20, 29, 24), RGB(25, 36, 30), RGB(30, 43, 36), RGB(58, 86, 72), RGB(243, 247, 243), RGB(160, 181, 168), RGB(86, 196, 132), RGB(61, 190, 119), RGB(246, 190, 72), RGB(255, 110, 101), RGB(38, 52, 44), RGB(51, 65, 55), RGB(26, 41, 33), RGB(88, 118, 98), RGB(18, 24, 21)}
};

constexpr int kThemeA = 0;
constexpr int kThemeB = 1;
constexpr int kThemeC = 2;

constexpr int ID_SET_THEME   = 201;
constexpr int ID_SET_ROM      = 202;
constexpr int ID_SET_BG       = 203;
constexpr int ID_SET_SAVE     = 204;
constexpr int ID_SET_RESET    = 205;
constexpr int ID_SET_CLOSE    = 206;

HWND g_hSettingsWnd{};
HWND g_hSetTheme{};
HWND g_hSetRom{};
HWND g_hSetBg{};
HWND g_hSetSave{};
HWND g_hSetReset{};
HWND g_hSetClose{};
HWND g_hSetRomBrowse{};
HWND g_hSetBgBrowse{};
ULONG_PTR g_GdiPlusToken{};
bool g_GdiPlusReady{};
bool g_SettingsSyncing{};
std::unique_ptr<Image> g_BackgroundImage;
std::wstring g_BackgroundResolvedPath;

std::wstring TrimCopy(std::wstring s) {
    auto notSpace = [](wchar_t ch) { return !iswspace(ch); };
    while (!s.empty() && !notSpace(s.front())) {
        s.erase(s.begin());
    }
    while (!s.empty() && !notSpace(s.back())) {
        s.pop_back();
    }
    if (s.size() >= 2) {
        const wchar_t first = s.front();
        const wchar_t last = s.back();
        if ((first == L'"' && last == L'"') || (first == L'\'' && last == L'\'')) {
            s = s.substr(1, s.size() - 2);
        }
    }
    return s;
}

bool DirectoryExistsW(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring BrowseForFolderPath(HWND owner) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    bi.lpszTitle = L"ROMフォルダを選択してください";
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return {};
    }

    wchar_t path[MAX_PATH]{};
    std::wstring result;
    if (SHGetPathFromIDListW(pidl, path)) {
        result = path;
    }
    CoTaskMemFree(pidl);
    return result;
}

std::wstring BrowseForImageFile(HWND owner) {
    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool needUninit = SUCCEEDED(co);
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) {
        if (needUninit) {
            CoUninitialize();
        }
        return {};
    }

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);

    const COMDLG_FILTERSPEC filters[] = {
        {L"画像ファイル", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp"},
        {L"すべてのファイル", L"*.*"}
    };
    dlg->SetFileTypes(static_cast<UINT>(sizeof(filters) / sizeof(filters[0])), filters);
    dlg->SetDefaultExtension(L"png");
    dlg->SetTitle(L"背景画像を選択してください");

    std::wstring result;
    hr = dlg->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR filePath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath)) && filePath) {
                result = filePath;
                CoTaskMemFree(filePath);
            }
            item->Release();
        }
    }

    dlg->Release();
    if (needUninit) {
        CoUninitialize();
    }
    return result;
}

std::wstring ToLowerCopy(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return s;
}

bool FileExistsW(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring ConfigIniPathW() {
    return ModuleDirW() + L"\\config.ini";
}

void EnsureGdiPlus() {
    if (g_GdiPlusReady) {
        return;
    }
    GdiplusStartupInput input{};
    input.GdiplusVersion = 1;
    if (GdiplusStartup(&g_GdiPlusToken, &input, nullptr) == Ok) {
        g_GdiPlusReady = true;
    }
}

int ThemeIndexFromKey(const std::wstring& key) {
    const std::wstring k = ToLowerCopy(TrimCopy(key));
    if (k == L"b") return kThemeB;
    if (k == L"c") return kThemeC;
    return kThemeA;
}

std::wstring ThemeKeyFromIndex(int idx) {
    if (idx == kThemeB) return L"b";
    if (idx == kThemeC) return L"c";
    return L"a";
}

void ApplyPalette(const ThemePalette& p) {
    C_BG = p.bg;
    C_BG2 = p.bg2;
    C_PANEL = p.panel;
    C_PANEL2 = p.panel2;
    C_LINE = p.line;
    C_TEXT = p.text;
    C_MUTED = p.muted;
    C_ACCENT = p.accent;
    C_SUCCESS = p.success;
    C_WARNING = p.warning;
    C_DANGER = p.danger;
    C_BTN = p.btn;
    C_BTN_HOV = p.btnHov;
    C_BTN_DN = p.btnDn;
    C_BTN_EDGE = p.btnEdge;
    C_EDIT_BG = p.editBg;

    SafeDeleteObject(g_brPanel);
    SafeDeleteObject(g_brPanel2);
    SafeDeleteObject(g_brEdit);
    g_brPanel = CreateSolidBrush(C_PANEL);
    g_brPanel2 = CreateSolidBrush(C_PANEL2);
    g_brEdit = CreateSolidBrush(C_EDIT_BG);

    if (hLog) {
        SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, C_EDIT_BG);
    }

    if (g_hMain) {
        InvalidateRect(g_hMain, nullptr, TRUE);
    }
    if (g_hSettingsWnd) {
        InvalidateRect(g_hSettingsWnd, nullptr, TRUE);
    }
}

void ReloadBackgroundImage() {
    if (g_ConfigBackgroundImage.empty()) {
        g_BackgroundImage.reset();
        g_BackgroundResolvedPath.clear();
        return;
    }

    const std::wstring resolved = ResolveAppPathW(TrimCopy(g_ConfigBackgroundImage));
    if (resolved.empty() || !FileExistsW(resolved)) {
        g_BackgroundImage.reset();
        g_BackgroundResolvedPath.clear();
        return;
    }

    if (g_BackgroundImage && resolved == g_BackgroundResolvedPath) {
        return;
    }

    EnsureGdiPlus();

    std::unique_ptr<Image> image(Image::FromFile(resolved.c_str(), FALSE));
    if (!image || image->GetLastStatus() != Ok) {
        g_BackgroundImage.reset();
        g_BackgroundResolvedPath.clear();
        return;
    }

    g_BackgroundResolvedPath = resolved;
    g_BackgroundImage = std::move(image);
}

void SyncMainTexts() {
    g_RomText = g_ConfigRomDir.empty() ? L"./TAB-A05-BD" : g_ConfigRomDir;
    UpdateText(hLblRom, g_RomText);
    UpdateText(hLblFastboot, g_FastbootText);
}

void SyncSettingsWindowFields() {
    if (!g_hSettingsWnd) {
        return;
    }

    g_SettingsSyncing = true;

    const int themeIndex = ThemeIndexFromKey(g_ConfigThemeKey);
    if (g_hSetTheme) {
        SendMessageW(g_hSetTheme, CB_SETCURSEL, themeIndex, 0);
    }
    if (g_hSetRom) {
        SetWindowTextW(g_hSetRom, g_ConfigRomDir.empty() ? L"./TAB-A05-BD" : g_ConfigRomDir.c_str());
    }
    if (g_hSetBg) {
        SetWindowTextW(g_hSetBg, g_ConfigBackgroundImage.c_str());
    }

    g_SettingsSyncing = false;
}

void ApplyConfigState(bool saveToDisk) {
    g_ConfigThemeKey = ThemeKeyFromIndex(ThemeIndexFromKey(g_ConfigThemeKey));
    ApplyPalette(kThemes[ThemeIndexFromKey(g_ConfigThemeKey)]);
    ReloadBackgroundImage();
    SyncMainTexts();
    if (saveToDisk) {
        SaveAppConfig();
    }
    SyncSettingsWindowFields();
}

std::wstring ReadEditText(HWND hwnd) {
    if (!hwnd) {
        return {};
    }
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(len + 1), L'\0');
    if (len > 0) {
        GetWindowTextW(hwnd, text.data(), len + 1);
        text.resize(static_cast<size_t>(len));
    } else {
        text.clear();
    }
    return TrimCopy(text);
}

void ApplyFromSettingsControls(bool saveToDisk) {
    if (!g_hSetTheme || !g_hSetRom || !g_hSetBg) {
        return;
    }

    const int sel = static_cast<int>(SendMessageW(g_hSetTheme, CB_GETCURSEL, 0, 0));
    g_ConfigThemeKey = ThemeKeyFromIndex(sel);

    std::wstring rom = ReadEditText(g_hSetRom);
    if (rom.empty()) {
        rom = L"./TAB-A05-BD";
    }
    g_ConfigRomDir = rom;

    std::wstring bg = ReadEditText(g_hSetBg);
    g_ConfigBackgroundImage = bg;

    ApplyConfigState(saveToDisk);
}

void SetDefaults() {
    g_ConfigThemeKey = L"a";
    g_ConfigRomDir = L"./TAB-A05-BD";
    g_ConfigBackgroundImage.clear();
    ApplyConfigState(false);
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        const HFONT font = g_hFontBody ? g_hFontBody : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        CreateWindowExW(0, L"STATIC", L"色パターン", WS_CHILD | WS_VISIBLE,
                        18, 18, 100, 20, hwnd, nullptr, nullptr, nullptr);
        g_hSetTheme = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                      120, 14, 150, 180, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_SET_THEME)), nullptr, nullptr);
        SendMessageW(g_hSetTheme, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SendMessageW(g_hSetTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"a"));
        SendMessageW(g_hSetTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"b"));
        SendMessageW(g_hSetTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"c"));

        CreateWindowExW(0, L"STATIC", L"ROMフォルダ", WS_CHILD | WS_VISIBLE,
                        18, 68, 100, 20, hwnd, nullptr, nullptr, nullptr);
        g_hSetRom = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                    120, 64, 304, 24, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_SET_ROM)), nullptr, nullptr);
        SendMessageW(g_hSetRom, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        g_hSetRomBrowse = CreateWindowExW(0, L"BUTTON", L"参照", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          434, 63, 68, 26, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_SET_ROM_BROWSE)), nullptr, nullptr);
        SendMessageW(g_hSetRomBrowse, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);

        CreateWindowExW(0, L"STATIC", L"背景画像", WS_CHILD | WS_VISIBLE,
                        18, 106, 100, 20, hwnd, nullptr, nullptr, nullptr);
        g_hSetBg = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                   120, 102, 304, 24, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_SET_BG)), nullptr, nullptr);
        SendMessageW(g_hSetBg, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        g_hSetBgBrowse = CreateWindowExW(0, L"BUTTON", L"画像選択", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         434, 101, 68, 26, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_SET_BG_BROWSE)), nullptr, nullptr);
        SendMessageW(g_hSetBgBrowse, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);

        g_hSetSave = CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     120, 162, 90, 30, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_SET_SAVE)), nullptr, nullptr);
        g_hSetReset = CreateWindowExW(0, L"BUTTON", L"初期化", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      222, 162, 90, 30, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_SET_RESET)), nullptr, nullptr);
        g_hSetClose = CreateWindowExW(0, L"BUTTON", L"閉じる", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      324, 162, 90, 30, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_SET_CLOSE)), nullptr, nullptr);
        SendMessageW(g_hSetSave, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SendMessageW(g_hSetReset, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SendMessageW(g_hSetClose, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);

        SyncSettingsWindowFields();
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_SET_SAVE:
            ApplyFromSettingsControls(true);
            MessageBoxW(hwnd, L"設定を保存しました。", L"保存完了", MB_ICONINFORMATION);
            return 0;
        case ID_SET_RESET:
            SetDefaults();
            SyncSettingsWindowFields();
            SaveAppConfig();
            MessageBoxW(hwnd, L"設定を初期値に戻しました。", L"初期化", MB_ICONINFORMATION);
            return 0;
        case ID_SET_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case ID_SET_THEME:
            if (!g_SettingsSyncing && HIWORD(wp) == CBN_SELCHANGE) {
                ApplyFromSettingsControls(true);
            }
            return 0;
        case ID_SET_ROM:
            if (!g_SettingsSyncing && (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == EN_KILLFOCUS)) {
                ApplyFromSettingsControls(true);
            }
            return 0;
        case ID_SET_BG:
            if (!g_SettingsSyncing && (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == EN_KILLFOCUS)) {
                ApplyFromSettingsControls(true);
            }
            return 0;
        case ID_SET_ROM_BROWSE: {
            const std::wstring picked = BrowseForFolderPath(hwnd);
            if (!picked.empty()) {
                SetWindowTextW(g_hSetRom, picked.c_str());
                ApplyFromSettingsControls(true);
            }
            return 0;
        }
        case ID_SET_BG_BROWSE: {
            const std::wstring picked = BrowseForImageFile(hwnd);
            if (!picked.empty()) {
                SetWindowTextW(g_hSetBg, picked.c_str());
                ApplyFromSettingsControls(true);
            }
            return 0;
        }
        default:
            break;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brTransparent);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brTransparent);
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, C_PANEL);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brPanel);
    }

    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(C_BG);
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(C_BG);
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, g_hFontTitle ? g_hFontTitle : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        SetTextColor(hdc, C_TEXT);
        RECT title{18, 14, rc.right - 18, 38};
        DrawTextW(hdc, L"設定", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, g_hFontBody ? g_hFontBody : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        SetTextColor(hdc, C_MUTED);
        RECT sub{18, 40, rc.right - 18, 64};
        DrawTextW(hdc, L"config.ini を同じディレクトリに保存します。", -1, &sub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        if (g_hMain && IsWindow(g_hMain)) {
            EnableWindow(g_hMain, TRUE);
            SetForegroundWindow(g_hMain);
        }
        g_hSettingsWnd = nullptr;
        g_hSetTheme = nullptr;
        g_hSetRom = nullptr;
        g_hSetBg = nullptr;
        g_hSetSave = nullptr;
        g_hSetReset = nullptr;
        g_hSetClose = nullptr;
        g_hSetRomBrowse = nullptr;
        g_hSetBgBrowse = nullptr;
        g_SettingsSyncing = false;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void DrawBackgroundImage(HDC hdc, const RECT& rc) {
    if (!g_BackgroundImage) {
        return;
    }

    const UINT width = g_BackgroundImage->GetWidth();
    const UINT height = g_BackgroundImage->GetHeight();
    if (width == 0 || height == 0) {
        return;
    }

    Graphics g(hdc);
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    ImageAttributes attrs;
    ColorMatrix matrix = {{
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.18f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    }};
    attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);

    const RectF dest(static_cast<REAL>(rc.left), static_cast<REAL>(rc.top),
                     static_cast<REAL>(rc.right - rc.left), static_cast<REAL>(rc.bottom - rc.top));
    g.DrawImage(g_BackgroundImage.get(), dest,
                0.0f, 0.0f, static_cast<REAL>(width), static_cast<REAL>(height), UnitPixel, &attrs);
}

void LoadAppConfig() {
    const std::wstring path = ConfigIniPathW();
    const bool exists = FileExistsW(path);

    wchar_t buffer[1024]{};
    GetPrivateProfileStringW(L"Settings", L"theme", L"a", buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), path.c_str());
    g_ConfigThemeKey = ThemeKeyFromIndex(ThemeIndexFromKey(buffer));

    GetPrivateProfileStringW(L"Settings", L"rom_dir", L"./TAB-A05-BD", buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), path.c_str());
    g_ConfigRomDir = TrimCopy(buffer);
    if (g_ConfigRomDir.empty()) {
        g_ConfigRomDir = L"./TAB-A05-BD";
    }

    GetPrivateProfileStringW(L"Settings", L"background_image", L"", buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), path.c_str());
    g_ConfigBackgroundImage = TrimCopy(buffer);

    ApplyConfigState(false);

    if (!exists) {
        SaveAppConfig();
    }
}

void SaveAppConfig() {
    const std::wstring path = ConfigIniPathW();
    const std::wstring theme = ThemeKeyFromIndex(ThemeIndexFromKey(g_ConfigThemeKey));
    const std::wstring rom = g_ConfigRomDir.empty() ? L"./TAB-A05-BD" : TrimCopy(g_ConfigRomDir);
    const std::wstring bg = TrimCopy(g_ConfigBackgroundImage);

    WritePrivateProfileStringW(L"Settings", L"theme", theme.c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"rom_dir", rom.c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"background_image", bg.c_str(), path.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());

    g_ConfigThemeKey = theme;
    g_ConfigRomDir = rom;
    g_ConfigBackgroundImage = bg;
}

void OpenSettingsWindow() {
    LoadAppConfig();

    if (g_hSettingsWnd && IsWindow(g_hSettingsWnd)) {
        SyncSettingsWindowFields();
        ShowWindow(g_hSettingsWnd, SW_SHOW);
        SetForegroundWindow(g_hSettingsWnd);
        return;
    }

    if (!g_hMain) {
        return;
    }


    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"A05BDSettingsWnd";
        wc.hbrBackground = nullptr;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassW(&wc);
        registered = true;
    }

    RECT rc{};
    GetWindowRect(g_hMain, &rc);
    const int w = 540;
    const int h = 292;
    const int x = rc.left + ((rc.right - rc.left) - w) / 2;
    const int y = rc.top + ((rc.bottom - rc.top) - h) / 2;

    g_hSettingsWnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
                                     L"A05BDSettingsWnd", L"設定",
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                     x, y, w, h, g_hMain, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_hSettingsWnd) {
        EnableWindow(g_hMain, FALSE);
        ShowWindow(g_hSettingsWnd, SW_SHOW);
        UpdateWindow(g_hSettingsWnd);
    }
}

void CleanupOptions() {
    if (g_hSettingsWnd && IsWindow(g_hSettingsWnd)) {
        DestroyWindow(g_hSettingsWnd);
    }

    g_BackgroundImage.reset();
    g_BackgroundResolvedPath.clear();

    if (g_GdiPlusReady) {
        GdiplusShutdown(g_GdiPlusToken);
        g_GdiPlusToken = 0;
        g_GdiPlusReady = false;
    }
}
