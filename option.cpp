#include "app.h"
#include <shobjidl.h>

constexpr int ID_CB_THEME = 201;
constexpr int ID_ED_ROM = 202;
constexpr int ID_BTN_ROM = 203;
constexpr int ID_ED_BG = 204;
constexpr int ID_BTN_BG = 205;
constexpr int ID_BTN_OK = 206;
constexpr int ID_BTN_CANCEL = 207;
constexpr int ID_BTN_RESET = 208;

std::wstring ConfigIniPath() {
    return ModuleDirW() + L"\\config.ini";
}

void LoadConfig() {
    std::wstring path = ConfigIniPath();
    g_Config.theme = GetPrivateProfileIntW(L"Settings", L"Theme", 0, path.c_str());
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(L"Settings", L"RomDir", L"", buf, MAX_PATH, path.c_str());
    g_Config.romDir = buf;
    GetPrivateProfileStringW(L"Settings", L"BgImage", L"", buf, MAX_PATH, path.c_str());
    g_Config.bgImage = buf;
}

void SaveConfig() {
    std::wstring path = ConfigIniPath();
    WritePrivateProfileStringW(L"Settings", L"Theme", std::to_wstring(g_Config.theme).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"RomDir", g_Config.romDir.c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"BgImage", g_Config.bgImage.c_str(), path.c_str());
}

void ApplyTheme() {
    if (g_Config.theme == 1) {
        C_BG = RGB(240, 240, 240); C_BG2 = RGB(220, 220, 220); C_PANEL = RGB(255, 255, 255);
        C_PANEL2 = RGB(245, 245, 245); C_LINE = RGB(200, 200, 200); C_TEXT = RGB(30, 30, 30);
        C_MUTED = RGB(100, 100, 100); C_ACCENT = RGB(0, 120, 215); C_BTN = RGB(225, 225, 225);
        C_BTN_HOV = RGB(210, 210, 210); C_BTN_DN = RGB(190, 190, 190); C_BTN_EDGE = RGB(170, 170, 170);
        C_EDIT_BG = RGB(250, 250, 250);
    } else if (g_Config.theme == 2) {
        C_BG = RGB(10, 10, 10); C_BG2 = RGB(15, 15, 15); C_PANEL = RGB(20, 20, 20);
        C_PANEL2 = RGB(25, 25, 25); C_LINE = RGB(0, 100, 0); C_TEXT = RGB(0, 255, 0);
        C_MUTED = RGB(0, 150, 0); C_ACCENT = RGB(0, 200, 0); C_BTN = RGB(15, 30, 15);
        C_BTN_HOV = RGB(20, 50, 20); C_BTN_DN = RGB(10, 20, 10); C_BTN_EDGE = RGB(0, 150, 0);
        C_EDIT_BG = RGB(5, 15, 5);
    } else {
        C_BG = RGB(15, 18, 28); C_BG2 = RGB(21, 26, 40); C_PANEL = RGB(25, 31, 48);
        C_PANEL2 = RGB(30, 38, 58); C_LINE = RGB(56, 69, 98); C_TEXT = RGB(237, 242, 247);
        C_MUTED = RGB(155, 168, 190); C_ACCENT = RGB(0, 179, 255); C_BTN = RGB(39, 49, 72);
        C_BTN_HOV = RGB(52, 63, 92); C_BTN_DN = RGB(26, 37, 60); C_BTN_EDGE = RGB(86, 102, 138);
        C_EDIT_BG = RGB(17, 21, 31);
    }
}

void RefreshBrushes() {
    SafeDeleteObject(g_brPanel);
    SafeDeleteObject(g_brPanel2);
    SafeDeleteObject(g_brEdit);
    g_brPanel = CreateSolidBrush(C_PANEL);
    g_brPanel2 = CreateSolidBrush(C_PANEL2);
    g_brEdit = CreateSolidBrush(C_EDIT_BG);
    if (hLog) {
        SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, C_EDIT_BG);
        InvalidateRect(hLog, nullptr, TRUE);
    }
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

std::wstring BrowseFolder(HWND owner) {
    std::wstring out;
    IFileOpenDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            if (SUCCEEDED(pfd->Show(owner))) {
                IShellItem* psi = nullptr;
                if (SUCCEEDED(pfd->GetResult(&psi))) {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                        out = pszPath;
                        CoTaskMemFree(pszPath);
                    }
                    psi->Release();
                }
            }
        }
        pfd->Release();
    }
    return out;
}

std::wstring BrowseFile(HWND owner) {
    std::wstring out;
    IFileOpenDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        COMDLG_FILTERSPEC filters[] = { {L"Image Files", L"*.bmp;*.jpg;*.jpeg;*.png"}, {L"All Files", L"*.*"} };
        pfd->SetFileTypes(2, filters);
        if (SUCCEEDED(pfd->Show(owner))) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    out = pszPath;
                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    return out;
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"テーマ:", WS_CHILD | WS_VISIBLE, 20, 20, 100, 24, hwnd, nullptr, g_hFontBody);
        HWND cb = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 120, 20, 150, 100, hwnd, (HMENU)ID_CB_THEME, nullptr);
        SendMessageW(cb, WM_SETFONT, (WPARAM)g_hFontBody, 0);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"デフォルト");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"ライト");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"ハッカー");
        SendMessageW(cb, CB_SETCURSEL, g_Config.theme, 0);

        CreateWindowW(L"STATIC", L"ROMパス:", WS_CHILD | WS_VISIBLE, 20, 60, 100, 24, hwnd, nullptr, g_hFontBody);
        HWND edRom = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_Config.romDir.empty() ? L"(デフォルト)" : g_Config.romDir.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 120, 60, 240, 24, hwnd, (HMENU)ID_ED_ROM, nullptr);
        SendMessageW(edRom, WM_SETFONT, (WPARAM)g_hFontBody, 0);
        HWND btnRom = CreateWindowW(L"BUTTON", L"参照...", WS_CHILD | WS_VISIBLE, 370, 60, 60, 24, hwnd, (HMENU)ID_BTN_ROM, nullptr);
        SendMessageW(btnRom, WM_SETFONT, (WPARAM)g_hFontBody, 0);

        CreateWindowW(L"STATIC", L"背景画像:", WS_CHILD | WS_VISIBLE, 20, 100, 100, 24, hwnd, nullptr, g_hFontBody);
        HWND edBg = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_Config.bgImage.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 120, 100, 240, 24, hwnd, (HMENU)ID_ED_BG, nullptr);
        SendMessageW(edBg, WM_SETFONT, (WPARAM)g_hFontBody, 0);
        HWND btnBg = CreateWindowW(L"BUTTON", L"参照...", WS_CHILD | WS_VISIBLE, 370, 100, 60, 24, hwnd, (HMENU)ID_BTN_BG, nullptr);
        SendMessageW(btnBg, WM_SETFONT, (WPARAM)g_hFontBody, 0);

        HWND btnReset = CreateWindowW(L"BUTTON", L"初期化", WS_CHILD | WS_VISIBLE, 20, 160, 80, 30, hwnd, (HMENU)ID_BTN_RESET, nullptr);
        SendMessageW(btnReset, WM_SETFONT, (WPARAM)g_hFontBody, 0);
        HWND btnOk = CreateWindowW(L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE, 270, 160, 80, 30, hwnd, (HMENU)ID_BTN_OK, nullptr);
        SendMessageW(btnOk, WM_SETFONT, (WPARAM)g_hFontBody, 0);
        HWND btnCancel = CreateWindowW(L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE, 360, 160, 80, 30, hwnd, (HMENU)ID_BTN_CANCEL, nullptr);
        SendMessageW(btnCancel, WM_SETFONT, (WPARAM)g_hFontBody, 0);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wp) == ID_BTN_ROM) {
            std::wstring path = BrowseFolder(hwnd);
            if (!path.empty()) SetWindowTextW(GetDlgItem(hwnd, ID_ED_ROM), path.c_str());
        } else if (LOWORD(wp) == ID_BTN_BG) {
            std::wstring path = BrowseFile(hwnd);
            if (!path.empty()) SetWindowTextW(GetDlgItem(hwnd, ID_ED_BG), path.c_str());
        } else if (LOWORD(wp) == ID_BTN_RESET) {
            SendMessageW(GetDlgItem(hwnd, ID_CB_THEME), CB_SETCURSEL, 0, 0);
            SetWindowTextW(GetDlgItem(hwnd, ID_ED_ROM), L"(デフォルト)");
            SetWindowTextW(GetDlgItem(hwnd, ID_ED_BG), L"");
        } else if (LOWORD(wp) == ID_BTN_OK) {
            g_Config.theme = static_cast<int>(SendMessageW(GetDlgItem(hwnd, ID_CB_THEME), CB_GETCURSEL, 0, 0));
            wchar_t buf[MAX_PATH];
            GetWindowTextW(GetDlgItem(hwnd, ID_ED_ROM), buf, MAX_PATH);
            std::wstring r = buf;
            g_Config.romDir = (r == L"(デフォルト)") ? L"" : r;
            GetWindowTextW(GetDlgItem(hwnd, ID_ED_BG), buf, MAX_PATH);
            g_Config.bgImage = buf;
            SaveConfig();
            ApplyTheme();
            RefreshBrushes();
            InvalidateRect(g_hMain, nullptr, TRUE);
            g_RomText = g_Config.romDir.empty() ? L"./TAB-A05-BD" : g_Config.romDir;
            SetWindowTextW(hLblRom, g_RomText.c_str());
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == ID_BTN_CANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        EnableWindow(g_hMain, TRUE);
        SetForegroundWindow(g_hMain);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowSettingsDialog(HWND parent) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"A05SettingsClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    EnableWindow(parent, FALSE);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"A05SettingsClass", L"設定", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 480, 250, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    RECT rcParent, rcDlg;
    GetWindowRect(parent, &rcParent);
    GetWindowRect(hDlg, &rcDlg);
    SetWindowPos(hDlg, nullptr, rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2,
                 rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}
