#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

constexpr int  ID_BTN_CHECK  = 101;
constexpr int  ID_BTN_FLASH  = 102;
constexpr int  ID_PROGRESS   = 103;
constexpr int  ID_LOG_WINDOW = 104;
constexpr UINT WM_LOG_POST   = WM_APP + 1;
constexpr UINT WM_PROG_SET   = WM_APP + 2;
constexpr UINT WM_OP_DONE    = WM_APP + 3;

static const char    FASTBOOT[] = ".\\platform-tools\\fastboot.exe";
static const char    ROM_DIR[]  = "TAB-A05-BD";
static const char    TARGET_ID[]= "a05bd";
static const wchar_t WCLASS[]   = L"A05BDFlasher";

HWND g_hMain{}, hBtnCheck{}, hBtnFlash{}, hProgressBar{}, hLog{};
std::atomic<bool> g_DeviceVerified{false};
std::atomic<bool> g_Busy{false};

struct ExecResult { DWORD exitCode; std::string output; };

ExecResult Exec(const std::string& cmdLine) {
    ExecResult r{1, ""};
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hRead{}, hWrite{};
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return r;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si{sizeof(si)};
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::string cmd = cmdLine;
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead); CloseHandle(hWrite); return r;
    }
    CloseHandle(hWrite);
    char buf[512]; DWORD n;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &n, nullptr) && n) {
        buf[n] = '\0'; r.output += buf;
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &r.exitCode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return r;
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), n);
    return w;
}

void PostLog(const std::wstring& msg) {
    wchar_t* p = new wchar_t[msg.size() + 1];
    wmemcpy(p, msg.c_str(), msg.size() + 1);
    PostMessageW(g_hMain, WM_LOG_POST, 0, reinterpret_cast<LPARAM>(p));
}

void AppendLog(const wchar_t* text) {
    int i = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL,    static_cast<WPARAM>(i), static_cast<LPARAM>(i));
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text));
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"\r\n"));
}

std::string FB(const std::string& args) {
    return std::string(FASTBOOT) + " " + args;
}

std::string Img(const char* filename) {
    return std::string(ROM_DIR) + "\\" + filename;
}

void CheckThread() {
    PostLog(L"--- 端末確認 ---");
    PostLog(L"fastboot デバイスをスキャン中...");

    auto d = Exec(FB("devices"));
    std::string dl = d.output;
    std::transform(dl.begin(), dl.end(), dl.begin(),
                   [](unsigned char c){ return static_cast<char>(tolower(c)); });

    if (dl.find("fastboot") == std::string::npos) {
        PostLog(L"エラー: fastboot モードの端末が検出されません。");
        PostMessageW(g_hMain, WM_OP_DONE, 0, 0);
        g_Busy = false; return;
    }

    PostLog(L"端末検出。製品 ID を確認中...");
    auto v = Exec(FB("getvar product"));
    std::string vl = v.output;
    std::transform(vl.begin(), vl.end(), vl.begin(),
                   [](unsigned char c){ return static_cast<char>(tolower(c)); });

    if (vl.find(TARGET_ID) != std::string::npos) {
        g_DeviceVerified = true;
        PostLog(L"確認済み: " + ToWide(TARGET_ID));
        PostMessageW(g_hMain, WM_OP_DONE, 1, 0);
    } else {
        g_DeviceVerified = false;
        PostLog(L"エラー: モデル不一致。期待値: [" + ToWide(TARGET_ID) + L"]");
        PostLog(L"返答: " + ToWide(v.output));
        PostMessageW(g_hMain, WM_OP_DONE, 0, 0);
    }
    g_Busy = false;
}

void FlashThread() {
    struct Step { std::string cmd; const wchar_t* desc; };
    const std::vector<Step> steps = {
        { FB("flash partition " + Img("gpt.bin")),    L"1/5  GPT 書き込み中..."      },
        { FB("flash system "   + Img("system.img")),  L"2/5  System 書き込み中..."   },
        { FB("flash vendor "   + Img("vendor.img")),  L"3/5  Vendor 書き込み中..."   },
        { FB("flash vbmeta "   + Img("vbmeta.img")),  L"4/5  VBMeta 書き込み中..."   },
        { FB("flash recovery " + Img("recovery.img")),L"5/5  Recovery 書き込み中..." },
    };

    PostMessageW(g_hMain, WM_PROG_SET,
                 MAKEWPARAM(0, static_cast<WORD>(steps.size())), 0);

    PostLog(L"--- 書き込み開始 ---");
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        PostLog(steps[i].desc);
        auto r = Exec(steps[i].cmd);
        if (r.exitCode != 0) {
            PostLog(L"失敗  終了コード=" + std::to_wstring(r.exitCode));
            if (!r.output.empty()) PostLog(L"出力: " + ToWide(r.output));
            PostMessageW(g_hMain, WM_OP_DONE, 0, 1);
            g_Busy = false; return;
        }
        PostMessageW(g_hMain, WM_PROG_SET,
                     MAKEWPARAM(static_cast<WORD>(i + 1),
                                static_cast<WORD>(steps.size())), 0);
    }

    PostLog(L"==============================");
    PostLog(L"すべてのパーティションの書き込みに成功しました。");
    PostMessageW(g_hMain, WM_OP_DONE, 1, 1);
    g_Busy = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hGuiFont  = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT hMonoFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                      ANSI_CHARSET, 0, 0, DEFAULT_QUALITY,
                                      FIXED_PITCH, L"Consolas");

        auto MkCtrl = [&](const wchar_t* cls, const wchar_t* txt, DWORD sty,
                          int x, int y, int w, int h, HMENU id, HFONT font) -> HWND {
            HWND hw = CreateWindowW(cls, txt, WS_VISIBLE | WS_CHILD | sty,
                                    x, y, w, h, hwnd, id, nullptr, nullptr);
            SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            return hw;
        };

        MkCtrl(L"STATIC",
               L"対象: a05bd  |  fastboot: .\\platform-tools\\  |  ROM: TAB-A05-BD\\",
               0, 12, 10, 464, 18, nullptr, hGuiFont);

        hBtnCheck = MkCtrl(L"BUTTON", L"1. 端末確認",
                           BS_PUSHBUTTON, 12, 36, 160, 32,
                           reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_BTN_CHECK)),
                           hGuiFont);
        hBtnFlash = MkCtrl(L"BUTTON", L"2. ROM 書き込み",
                           BS_PUSHBUTTON | WS_DISABLED, 184, 36, 160, 32,
                           reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_BTN_FLASH)),
                           hGuiFont);

        hProgressBar = CreateWindowW(L"msctls_progress32", nullptr,
                                     WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
                                     12, 80, 464, 18, hwnd,
                                     reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_PROGRESS)),
                                     nullptr, nullptr);

        hLog = MkCtrl(L"EDIT", L"",
                      WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                      12, 108, 464, 228,
                      reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_LOG_WINDOW)),
                      hMonoFont);

        AppendLog(L"fastboot    : .\\platform-tools\\fastboot.exe");
        AppendLog(L"ROM フォルダ: TAB-A05-BD\\");
        AppendLog(L"書き込み対象: partition / system / vendor / vbmeta / recovery");
        AppendLog(L"--------------------------------------------------------------");
        AppendLog(L"端末を fastboot モードで接続し、[端末確認] を押してください。");
        break;
    }
    case WM_COMMAND:
        if (g_Busy) break;
        if (LOWORD(wp) == ID_BTN_CHECK) {
            g_DeviceVerified = false;
            g_Busy = true;
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            SendMessageW(hProgressBar, PBM_SETPOS, 0, 0);
            std::thread(CheckThread).detach();
        } else if (LOWORD(wp) == ID_BTN_FLASH && g_DeviceVerified) {
            g_Busy = true;
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            std::thread(FlashThread).detach();
        }
        break;

    case WM_LOG_POST: {
        wchar_t* p = reinterpret_cast<wchar_t*>(lp);
        AppendLog(p);
        delete[] p;
        break;
    }
    case WM_PROG_SET: {
        WORD pos = LOWORD(wp), rng = HIWORD(wp);
        SendMessageW(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, rng));
        SendMessageW(hProgressBar, PBM_SETPOS, pos, 0);
        break;
    }
    case WM_OP_DONE: {
        bool ok      = (wp != 0);
        bool isFlash = (lp != 0);
        EnableWindow(hBtnCheck, TRUE);
        if (!isFlash) {
            EnableWindow(hBtnFlash, ok ? TRUE : FALSE);
            if (ok) MessageBoxW(hwnd,
                                (L"端末を確認しました: " + ToWide(TARGET_ID)).c_str(),
                                L"確認完了", MB_ICONINFORMATION);
            else    MessageBoxW(hwnd,
                                L"端末が見つからないか、モデルが一致しません。\n"
                                L"TAB-A05-BD を fastboot モードで接続してください。",
                                L"確認失敗", MB_ICONERROR);
        } else {
            g_DeviceVerified = false;
            EnableWindow(hBtnFlash, FALSE);
            if (ok) MessageBoxW(hwnd,
                                L"すべての書き込みに成功しました。",
                                L"書き込み完了", MB_ICONINFORMATION);
            else    MessageBoxW(hwnd,
                                L"書き込みに失敗しました。ログを確認してください。",
                                L"書き込みエラー", MB_ICONERROR);
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = WCLASS;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);

    g_hMain = CreateWindowW(WCLASS, L"a05bd flasher v1.0",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            CW_USEDEFAULT, CW_USEDEFAULT, 504, 390,
                            nullptr, nullptr, hInst, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
