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

static const char    FASTBOOT[]  = ".\\platform-tools\\fastboot.exe";
static const char    ROM_DIR[]   = "TAB-A05-BD";
static const char    TARGET_ID[] = "a05bd";
static const wchar_t APP_CLASS[] = L"A05BDFlasher";

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
    SendMessageW(hLog, EM_SETSEL,     static_cast<WPARAM>(i), static_cast<LPARAM>(i));
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
    PostLog(L"--- \u7AEF\u672B\u78BA\u8A8D ---");
    PostLog(L"fastboot \u30C7\u30D0\u30A4\u30B9\u3092\u30B9\u30AD\u30E3\u30F3\u4E2D\u2026");

    auto d = Exec(FB("devices"));
    std::string dl = d.output;
    std::transform(dl.begin(), dl.end(), dl.begin(),
                   [](unsigned char c){ return static_cast<char>(tolower(c)); });

    if (dl.find("fastboot") == std::string::npos) {
        PostLog(L"\u30A8\u30E9\u30FC: fastboot \u30E2\u30FC\u30C9\u306E\u7AEF\u672B\u304C\u691C\u51FA\u3055\u308C\u307E\u305B\u3093\u3002");
        PostMessageW(g_hMain, WM_OP_DONE, 0, 0);
        g_Busy = false; return;
    }

    PostLog(L"\u7AEF\u672B\u3092\u691C\u51FA\u3057\u307E\u3057\u305F\u3002\u88FD\u54C1 ID \u3092\u78BA\u8A8D\u4E2D\u2026");
    auto v = Exec(FB("getvar product"));
    std::string vl = v.output;
    std::transform(vl.begin(), vl.end(), vl.begin(),
                   [](unsigned char c){ return static_cast<char>(tolower(c)); });

    if (vl.find(TARGET_ID) != std::string::npos) {
        g_DeviceVerified = true;
        PostLog(L"\u78BA\u8A8D\u6E08\u307F: " + ToWide(TARGET_ID));
        PostMessageW(g_hMain, WM_OP_DONE, 1, 0);
    } else {
        g_DeviceVerified = false;
        PostLog(L"\u30A8\u30E9\u30FC: \u30E2\u30C7\u30EB\u4E0D\u4E00\u81F4\u3002\u671F\u5F85\u5024: [" + ToWide(TARGET_ID) + L"]");
        PostLog(L"\u8FD4\u7B54: " + ToWide(v.output));
        PostMessageW(g_hMain, WM_OP_DONE, 0, 0);
    }
    g_Busy = false;
}

void FlashThread() {
    struct Step { std::string cmd; const wchar_t* desc; };
    const std::vector<Step> steps = {
        { FB("flash partition " + Img("gpt.bin")),
          L"1/5  GPT \u66F8\u304D\u8FBC\u307F\u4E2D\u2026"      },
        { FB("flash system "   + Img("system.img")),
          L"2/5  System \u66F8\u304D\u8FBC\u307F\u4E2D\u2026"   },
        { FB("flash vendor "   + Img("vendor.img")),
          L"3/5  Vendor \u66F8\u304D\u8FBC\u307F\u4E2D\u2026"   },
        { FB("flash vbmeta "   + Img("vbmeta.img")),
          L"4/5  VBMeta \u66F8\u304D\u8FBC\u307F\u4E2D\u2026"   },
        { FB("flash recovery " + Img("recovery.img")),
          L"5/5  Recovery \u66F8\u304D\u8FBC\u307F\u4E2D\u2026" },
    };

    PostMessageW(g_hMain, WM_PROG_SET,
                 MAKEWPARAM(0, static_cast<WORD>(steps.size())), 0);

    PostLog(L"--- \u66F8\u304D\u8FBC\u307F\u958B\u59CB ---");
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        PostLog(steps[i].desc);
        auto r = Exec(steps[i].cmd);
        if (r.exitCode != 0) {
            PostLog(L"\u5931\u6557  \u7D42\u4E86\u30B3\u30FC\u30C9=" + std::to_wstring(r.exitCode));
            if (!r.output.empty()) PostLog(L"\u51FA\u529B: " + ToWide(r.output));
            PostMessageW(g_hMain, WM_OP_DONE, 0, 1);
            g_Busy = false; return;
        }
        PostMessageW(g_hMain, WM_PROG_SET,
                     MAKEWPARAM(static_cast<WORD>(i + 1),
                                static_cast<WORD>(steps.size())), 0);
    }

    PostLog(L"==============================");
    PostLog(L"\u3059\u3079\u3066\u306E\u30D1\u30FC\u30C6\u30A3\u30B7\u30E7\u30F3\u306E\u66F8\u304D\u8FBC\u307F\u306B\u6210\u529F\u3057\u307E\u3057\u305F\u3002");
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
               L"\u5BFE\u8C61: a05bd  |  fastboot: .\\platform-tools\\  |  ROM: TAB-A05-BD\\",
               0, 12, 10, 464, 18, nullptr, hGuiFont);

        hBtnCheck = MkCtrl(L"BUTTON",
                           L"1. \u7AEF\u672B\u78BA\u8A8D",
                           BS_PUSHBUTTON, 12, 36, 160, 32,
                           reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_BTN_CHECK)),
                           hGuiFont);
        hBtnFlash = MkCtrl(L"BUTTON",
                           L"2. ROM \u66F8\u304D\u8FBC\u307F",
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
        AppendLog(L"ROM \u30D5\u30A9\u30EB\u30C0: TAB-A05-BD\\");
        AppendLog(L"\u66F8\u304D\u8FBC\u307F\u5BFE\u8C61: partition / system / vendor / vbmeta / recovery");
        AppendLog(L"--------------------------------------------------------------");
        AppendLog(L"\u7AEF\u672B\u3092 fastboot \u30E2\u30FC\u30C9\u3067\u63A5\u7D9A\u3057\u3001[\u7AEF\u672B\u78BA\u8A8D] \u3092\u62BC\u3057\u3066\u304F\u3060\u3055\u3044\u3002");
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
                                (L"\u7AEF\u672B\u3092\u78BA\u8A8D\u3057\u307E\u3057\u305F: " + ToWide(TARGET_ID)).c_str(),
                                L"\u78BA\u8A8D\u5B8C\u4E86", MB_ICONINFORMATION);
            else    MessageBoxW(hwnd,
                                L"\u7AEF\u672B\u304C\u898B\u3064\u304B\u3089\u306A\u3044\u304B\u3001\u30E2\u30C7\u30EB\u304C\u4E00\u81F4\u3057\u307E\u305B\u3093\u3002\n"
                                L"TAB-A05-BD \u3092 fastboot \u30E2\u30FC\u30C9\u3067\u63A5\u7D9A\u3057\u3066\u304F\u3060\u3055\u3044\u3002",
                                L"\u78BA\u8A8D\u5931\u6557", MB_ICONERROR);
        } else {
            g_DeviceVerified = false;
            EnableWindow(hBtnFlash, FALSE);
            if (ok) MessageBoxW(hwnd,
                                L"\u3059\u3079\u3066\u306E\u66F8\u304D\u8FBC\u307F\u306B\u6210\u529F\u3057\u307E\u3057\u305F\u3002",
                                L"\u66F8\u304D\u8FBC\u307F\u5B8C\u4E86", MB_ICONINFORMATION);
            else    MessageBoxW(hwnd,
                                L"\u66F8\u304D\u8FBC\u307F\u306B\u5931\u6557\u3057\u307E\u3057\u305F\u3002\u30ED\u30B0\u3092\u78BA\u8A8D\u3057\u3066\u304F\u3060\u3055\u3044\u3002",
                                L"\u66F8\u304D\u8FBC\u307F\u30A8\u30E9\u30FC", MB_ICONERROR);
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
    wc.lpszClassName = APP_CLASS;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);

    g_hMain = CreateWindowW(APP_CLASS,
                            L"a05bd \u30D5\u30E9\u30C3\u30B7\u30E3\u30FC v1.0",
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
