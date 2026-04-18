#include "app.h"

#include <thread>

static HWND CreateCtrl(HWND parent, const wchar_t* cls, const wchar_t* txt, DWORD style, DWORD exStyle,
                       int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(exStyle, cls, txt, WS_VISIBLE | WS_CHILD | style,
                             x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), nullptr, nullptr);
    if (c && font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
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
        hLblFastboot = CreateCtrl(hwnd, L"STATIC", L".\\platform-tools\\fastboot.exe", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_FASTBT, g_hFontBody);
        hLblRom = CreateCtrl(hwnd, L"STATIC", L"書き込み用フォルダ", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_ROM, g_hFontBody);
        hLblSteps = CreateCtrl(hwnd, L"STATIC", L"wipe / flash / erase / reboot", SS_LEFT, 0, 0, 0, 0, 0, ID_LBL_STEPS, g_hFontBody);

        hBtnCheck = CreateCtrl(hwnd, L"BUTTON", L"端末確認", BS_OWNERDRAW | BS_PUSHBUTTON, 0, 0, 0, 0, 0, ID_BTN_CHECK, g_hFontBody);
        hBtnFlash = CreateCtrl(hwnd, L"BUTTON", L"ROM 書き込み", BS_OWNERDRAW | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, 0, ID_BTN_FLASH, g_hFontBody);

        hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
                                       0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_PROGRESS)), nullptr, nullptr);
        SendMessageW(hProgressBar, PBM_SETMARQUEE, FALSE, 0);

        hLog = CreateCtrl(hwnd, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | ES_NOHIDESEL,
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
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
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
