#include "app.h"

#include <thread>

#include <commctrl.h>


namespace {

void PaintLogBackdrop(HWND hwnd, HDC hdc, const RECT& rcClient) {
    const int w = rcClient.right - rcClient.left;
    const int h = rcClient.bottom - rcClient.top;
    if (w <= 0 || h <= 0) {
        return;
    }

    HWND parent = GetParent(hwnd);
    if (!parent) {
        return;
    }

    RECT rcScreen = rcClient;
    POINT pts[2] = {
        {rcScreen.left, rcScreen.top},
        {rcScreen.right, rcScreen.bottom}
    };
    MapWindowPoints(hwnd, parent, pts, 2);
    rcScreen.left = pts[0].x;
    rcScreen.top = pts[0].y;
    rcScreen.right = pts[1].x;
    rcScreen.bottom = pts[1].y;

    HDC hParent = GetDC(parent);
    if (!hParent) {
        return;
    }

    BitBlt(hdc, 0, 0, w, h, hParent, rcScreen.left, rcScreen.top, SRCCOPY);
    ReleaseDC(parent, hParent);

    HDC mem = CreateCompatibleDC(hdc);
    if (!mem) {
        return;
    }

    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    if (!bmp) {
        DeleteDC(mem);
        return;
    }

    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    RECT fill{0, 0, w, h};
    HBRUSH br = CreateSolidBrush(C_PANEL2);
    FillRect(mem, &fill, br);
    DeleteObject(br);

    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 170;
    bf.AlphaFormat = 0;
    AlphaBlend(hdc, 0, 0, w, h, mem, 0, 0, w, h, bf);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT CALLBACK LogSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        PaintLogBackdrop(hwnd, hdc, rc);
        return 1;
    }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static HWND CreateCtrl(HWND parent, const wchar_t* cls, const wchar_t* txt, DWORD style, DWORD exStyle,
                       int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(exStyle, cls, txt, WS_VISIBLE | WS_CHILD | style,
                             x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), nullptr, nullptr);
    if (c && font) {
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    }
    return c;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hwnd;

        g_hFontTitle = MakeFont(L"Segoe UI", 22, FW_BOLD);
        g_hFontSub = MakeFont(L"Segoe UI", 10, FW_NORMAL);
        g_hFontBody = MakeFont(L"Segoe UI", 10, FW_NORMAL);
        g_hFontMono = MakeFont(L"Consolas", 10, FW_NORMAL);

        g_brPanel = CreateSolidBrush(C_PANEL);
        g_brPanel2 = CreateSolidBrush(C_PANEL2);
        g_brEdit = CreateSolidBrush(C_EDIT_BG);
        g_brTransparent = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));

        hLblTitle = CreateCtrl(hwnd, L"STATIC", L"a05bd フラッシャー", 0, 0, 0, 0, 0, 0, ID_LBL_TITLE, g_hFontTitle);
        hLblSub = CreateCtrl(hwnd, L"STATIC", L"簡易書き込みツール", 0, 0, 0, 0, 0, 0, ID_LBL_SUB, g_hFontSub);
        hLblStatus = CreateCtrl(hwnd, L"STATIC", L"待機中", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_STATUS, g_hFontBody);
        hLblHint = CreateCtrl(hwnd, L"STATIC", g_HintText.c_str(), SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_HINT, g_hFontBody);
        hLblDevice = CreateCtrl(hwnd, L"STATIC", L"未確認", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_DEVICE, g_hFontBody);
        hLblFastboot = CreateCtrl(hwnd, L"STATIC", g_FastbootText.c_str(), SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_FASTBT, g_hFontBody);
        hLblRom = CreateCtrl(hwnd, L"STATIC", g_RomText.c_str(), SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_ROM, g_hFontBody);
        hLblSteps = CreateCtrl(hwnd, L"STATIC", L"flash / erase / reboot", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_STEPS, g_hFontBody);

        hBtnCheck = CreateCtrl(hwnd, L"BUTTON", L"端末確認", BS_OWNERDRAW | BS_PUSHBUTTON, 0, 0, 0, 0, 0, ID_BTN_CHECK, g_hFontBody);
        hBtnFlash = CreateCtrl(hwnd, L"BUTTON", L"ROM 書き込み", BS_OWNERDRAW | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, 0, ID_BTN_FLASH, g_hFontBody);
        hBtnSettings = CreateCtrl(hwnd, L"BUTTON", L"設定", BS_OWNERDRAW | BS_PUSHBUTTON, 0, 0, 0, 0, 0, ID_BTN_SETTINGS, g_hFontBody);

        hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
                                       0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_PROGRESS)), nullptr, nullptr);
        SendMessageW(hProgressBar, PBM_SETMARQUEE, FALSE, 0);

        hLog = CreateCtrl(hwnd, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | ES_NOHIDESEL,
                          WS_EX_TRANSPARENT | WS_EX_CLIENTEDGE, 0, 0, 0, 0, ID_LOG_WINDOW, g_hFontMono);
        SetWindowSubclass(hLog, LogSubclassProc, 1, 0);
        SendMessageW(hLog, EM_SETLIMITTEXT, 0, 0);
        SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, C_EDIT_BG);

        UpdateStatusUI(L"待機中");
        UpdateDeviceUI(L"未確認");
        UpdateHintUI(L"端末を fastboot モードで接続してから「端末確認」を押してください。");
        UpdateStepsUI(L"flash / erase / reboot");

        AppendLogBlock(L"fastboot    : .\\platform-tools\\fastboot.exe\r\n"
                       L"ROMフォルダ : " + g_RomText + L"\r\n"
                       L"確認手順    : fastboot devices / getvar product / getvar unlocked\r\n"
                       L"処理方式    : 個別 partition に flash / erase を順次実行\r\n"
                       L"--------------------------------------------------------------\r\n"
                       L"端末を fastboot モードで接続し、「端末確認」を押してください。\r\n");

        if (!FileExistsA(FASTBOOT_EXE())) {
            UpdateStatusUI(L"fastboot 未検出");
            UpdateHintUI(L".\\platform-tools\\fastboot.exe を配置してください。");
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            AppendLogBlock(L"警告: fastboot.exe が見つかりません。\r\n");
        }

        LayoutControls(hwnd);
        return 0;
    }

    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = 780;
        mmi->ptMinTrackSize.y = 580;
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_BTN_SETTINGS) {
            OpenSettingsWindow();
            return 0;
        }
        if (g_Busy.load(std::memory_order_acquire)) {
            break;
        }
        if (LOWORD(wp) == ID_BTN_CHECK) {
            g_DeviceVerified = false;
            g_Unlocked = false;
            g_Busy = true;
            const uint32_t token = BeginOperation();
            UpdateStatusUI(L"端末確認中");
            UpdateDeviceUI(L"確認中…");
            UpdateHintUI(L"端末情報を取得しています。");
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            SendMessageW(hProgressBar, PBM_SETPOS, 0, 0);
            std::thread([token]() { CheckThread(token); }).detach();
        } else if (LOWORD(wp) == ID_BTN_FLASH && g_DeviceVerified && g_Unlocked) {
            g_Busy = true;
            const uint32_t token = BeginOperation();
            UpdateStatusUI(L"書き込み中");
            UpdateHintUI(L"書き込み処理を実行しています。");
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            std::thread([token]() { FlashThread(token); }).detach();
        }
        return 0;

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
        if (dis->CtlType != ODT_BUTTON) {
            break;
        }
        const bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
        const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
        DrawButtonFace(dis, hot, pressed, enabled);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        HWND ctrl = reinterpret_cast<HWND>(lp);

        COLORREF bg = C_PANEL;
        COLORREF fg = C_TEXT;

        if (ctrl == hLblStatus) {
            if (g_StatusText.find(L"書き込み中") != std::wstring::npos) fg = C_WARNING;
            else if (g_StatusText.find(L"失敗") != std::wstring::npos) fg = C_DANGER;
            else if (g_StatusText.find(L"完了") != std::wstring::npos || g_StatusText.find(L"確認済み") != std::wstring::npos) fg = C_SUCCESS;
            else if (g_StatusText.find(L"未検出") != std::wstring::npos) fg = C_DANGER;
            else fg = C_ACCENT;
        } else if (ctrl == hLblHint || ctrl == hLblSteps) {
            fg = C_MUTED;
        } else {
            fg = C_TEXT;
        }

        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, bg);
        SetTextColor(hdc, fg);
        return reinterpret_cast<LRESULT>(g_brPanel);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, C_EDIT_BG);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brEdit);
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, C_PANEL);
        SetTextColor(hdc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_brPanel);
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

    case WM_LOG_FLUSH:
        FlushLogQueue();
        return 0;

    case WM_TEXT_SET: {
        auto* msgp = reinterpret_cast<TextMessage*>(wp);
        if (!msgp) {
            return 0;
        }
        if (msgp->token == g_CurrentOperationToken.load(std::memory_order_acquire)) {
            if (msgp->kind == 0) UpdateStatusUI(msgp->text);
            else if (msgp->kind == 1) UpdateDeviceUI(msgp->text);
            else if (msgp->kind == 2) UpdateHintUI(msgp->text);
            else if (msgp->kind == 3) UpdateStepsUI(msgp->text);
        }
        delete msgp;
        return 0;
    }

    case WM_PROG_SET: {
        const uint32_t token = static_cast<uint32_t>(wp);
        if (token != g_CurrentOperationToken.load(std::memory_order_acquire)) {
            return 0;
        }
        const WORD pos = LOWORD(lp);
        const WORD rng = HIWORD(lp);
        SendMessageW(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, rng));
        SendMessageW(hProgressBar, PBM_SETPOS, pos, 0);
        return 0;
    }

    case WM_OP_DONE: {
        const uint32_t token = static_cast<uint32_t>(wp);
        if (token != g_CurrentOperationToken.load(std::memory_order_acquire)) {
            return 0;
        }

        const bool ok = (lp & 1) != 0;
        const bool flashMode = (lp & 2) != 0;

        g_Busy = false;
        EnableWindow(hBtnCheck, TRUE);

        if (!FileExistsA(FASTBOOT_EXE())) {
            EnableWindow(hBtnFlash, FALSE);
            UpdateStatusUI(L"fastboot 未検出");
            UpdateHintUI(L".\\platform-tools\\fastboot.exe を配置してください。");
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (!flashMode) {
            g_DeviceVerified = ok;
            g_Unlocked = ok;
            if (ok) {
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
                    MessageBoxW(hwnd, L"端末との接続に失敗しました", L"実行条件を満たしていない可能性があります。", MB_ICONWARNING);
                } else {
                    MessageBoxW(hwnd,
                        L"端末が見つからないか、モデルが一致しません。\n"
                        L"fastboot モードで接続してください。",
                        L"確認失敗", MB_ICONERROR);
                }
            }
        } else {
            EnableWindow(hBtnFlash, ok ? TRUE : FALSE);
            if (ok) {
                UpdateStatusUI(L"書き込み完了");
                UpdateHintUI(L"すべての処理が完了しました。");
                MessageBoxW(hwnd, L"書き込みが完了しました。", L"完了", MB_ICONINFORMATION);
            } else {
                UpdateStatusUI(L"書き込み失敗");
                UpdateHintUI(L"ログを確認してください。");
                MessageBoxW(hwnd, L"書き込みに失敗しました。ログを確認してください。", L"書き込みエラー", MB_ICONERROR);
            }
        }

        InvalidateRect(hwnd, nullptr, FALSE);
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
    LoadAppConfig();

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = APP_CLASS;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);

    g_hMain = CreateWindowExW(0, APP_CLASS, L"a05bd フラッシャー v1.2",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                              CW_USEDEFAULT, CW_USEDEFAULT, 920, 720,
                              nullptr, nullptr, hInst, nullptr);

    if (!g_hMain) {
        CleanupOptions();
        CleanupGdi();
        return 0;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CleanupOptions();
    CleanupGdi();
    return static_cast<int>(msg.wParam);
}
