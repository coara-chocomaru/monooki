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

static const char FASTBOOT[] = ".\\platform-tools\\fastboot.exe";
static const char ROM_DIR[]  = "TAB-A05-BD";
static const char TARGET_ID[]= "a05bd";

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

void PostLog(const std::string& msg) {
    char* p = new char[msg.size() + 1];
    memcpy(p, msg.c_str(), msg.size() + 1);
    PostMessage(g_hMain, WM_LOG_POST, 0, reinterpret_cast<LPARAM>(p));
}

void AppendLog(const char* text) {
    int i = GetWindowTextLength(hLog);
    SendMessage(hLog, EM_SETSEL, i, i);
    SendMessage(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text));
    SendMessage(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>("\r\n"));
}

std::string FB(const std::string& args) {
    return std::string(FASTBOOT) + " " + args;
}

std::string Img(const char* filename) {
    return std::string(ROM_DIR) + "\\" + filename;
}

void CheckThread() {
    PostLog("--- Device Check ---");
    PostLog("Scanning fastboot devices...");

    auto d = Exec(FB("devices"));
    std::string dl = d.output;
    std::transform(dl.begin(), dl.end(), dl.begin(),
                   [](unsigned char c){ return static_cast<char>(tolower(c)); });

    if (dl.find("fastboot") == std::string::npos) {
        PostLog("Error: No device detected in fastboot mode.");
        PostMessage(g_hMain, WM_OP_DONE, 0, 0);
        g_Busy = false; return;
    }

    PostLog("Device found. Querying product ID...");
    auto v = Exec(FB("getvar product"));
    std::string vl = v.output;
    std::transform(vl.begin(), vl.end(), vl.begin(),
                   [](unsigned char c){ return static_cast<char>(tolower(c)); });

    if (vl.find(TARGET_ID) != std::string::npos) {
        g_DeviceVerified = true;
        PostLog("Verified: " + std::string(TARGET_ID));
        PostMessage(g_hMain, WM_OP_DONE, 1, 0);
    } else {
        g_DeviceVerified = false;
        PostLog("Error: Model mismatch. Expected [" + std::string(TARGET_ID) + "]");
        PostLog("Returned: " + v.output);
        PostMessage(g_hMain, WM_OP_DONE, 0, 0);
    }
    g_Busy = false;
}

void FlashThread() {
    struct Step { std::string cmd; const char* desc; };
    const std::vector<Step> steps = {
        { FB("flash partition " + Img("gpt.bin")),    "1/5  Writing GPT..."      },
        { FB("flash system "   + Img("system.img")),  "2/5  Writing System..."   },
        { FB("flash vendor "   + Img("vendor.img")),  "3/5  Writing Vendor..."   },
        { FB("flash vbmeta "   + Img("vbmeta.img")),  "4/5  Writing VBMeta..."   },
        { FB("flash recovery " + Img("recovery.img")),"5/5  Writing Recovery..." },
    };

    PostMessage(g_hMain, WM_PROG_SET,
                MAKEWPARAM(0, static_cast<WORD>(steps.size())), 0);

    PostLog("--- Flash Start ---");
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        PostLog(steps[i].desc);
        auto r = Exec(steps[i].cmd);
        if (r.exitCode != 0) {
            PostLog("FAILED  exit=" + std::to_string(r.exitCode));
            if (!r.output.empty()) PostLog("Output: " + r.output);
            PostMessage(g_hMain, WM_OP_DONE, 0, 1);
            g_Busy = false; return;
        }
        PostMessage(g_hMain, WM_PROG_SET,
                    MAKEWPARAM(static_cast<WORD>(i + 1),
                               static_cast<WORD>(steps.size())), 0);
    }

    PostLog("==============================");
    PostLog("All partitions written successfully.");
    PostMessage(g_hMain, WM_OP_DONE, 1, 1);
    g_Busy = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hGuiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT hMonoFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                      ANSI_CHARSET, 0, 0, DEFAULT_QUALITY,
                                      FIXED_PITCH, "Consolas");

        auto MkCtrl = [&](const char* cls, const char* txt, DWORD sty,
                          int x, int y, int w, int h, HMENU id, HFONT font) -> HWND {
            HWND hw = CreateWindowA(cls, txt,
                                    WS_VISIBLE | WS_CHILD | sty,
                                    x, y, w, h, hwnd, id, nullptr, nullptr);
            SendMessage(hw, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            return hw;
        };

        MkCtrl("STATIC",
               "Target: a05bd  |  fastboot: .\\platform-tools\\  |  ROM: TAB-A05-BD\\",
               0, 12, 10, 464, 18, nullptr, hGuiFont);

        hBtnCheck = MkCtrl("BUTTON", "1. Check Device",
                           BS_PUSHBUTTON, 12, 36, 160, 32,
                           reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_BTN_CHECK)),
                           hGuiFont);
        hBtnFlash = MkCtrl("BUTTON", "2. Flash ROM",
                           BS_PUSHBUTTON | WS_DISABLED, 184, 36, 160, 32,
                           reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_BTN_FLASH)),
                           hGuiFont);

        hProgressBar = CreateWindowA(PROGRESS_CLASS, nullptr,
                                     WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
                                     12, 80, 464, 18,
                                     hwnd,
                                     reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_PROGRESS)),
                                     nullptr, nullptr);

        hLog = MkCtrl("EDIT", "",
                      WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                      12, 108, 464, 228,
                      reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_LOG_WINDOW)),
                      hMonoFont);

        AppendLog("fastboot : .\\platform-tools\\fastboot.exe");
        AppendLog("ROM dir  : TAB-A05-BD\\");
        AppendLog("Partitions: partition / system / vendor / vbmeta / recovery");
        AppendLog("--------------------------------------------------------------");
        AppendLog("Connect device in fastboot mode, then press [Check Device].");
        break;
    }
    case WM_COMMAND:
        if (g_Busy) break;
        if (LOWORD(wp) == ID_BTN_CHECK) {
            g_DeviceVerified = false;
            g_Busy = true;
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            SendMessage(hProgressBar, PBM_SETPOS, 0, 0);
            std::thread(CheckThread).detach();
        } else if (LOWORD(wp) == ID_BTN_FLASH && g_DeviceVerified) {
            g_Busy = true;
            EnableWindow(hBtnCheck, FALSE);
            EnableWindow(hBtnFlash, FALSE);
            std::thread(FlashThread).detach();
        }
        break;

    case WM_LOG_POST: {
        char* p = reinterpret_cast<char*>(lp);
        AppendLog(p);
        delete[] p;
        break;
    }
    case WM_PROG_SET: {
        WORD pos = LOWORD(wp), rng = HIWORD(wp);
        SendMessage(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, rng));
        SendMessage(hProgressBar, PBM_SETPOS, pos, 0);
        break;
    }
    case WM_OP_DONE: {
        bool ok      = (wp != 0);
        bool isFlash = (lp != 0);
        EnableWindow(hBtnCheck, TRUE);
        if (!isFlash) {
            EnableWindow(hBtnFlash, ok ? TRUE : FALSE);
            if (ok) MessageBoxA(hwnd,
                                ("Device confirmed: " + std::string(TARGET_ID)).c_str(),
                                "Check OK", MB_ICONINFORMATION);
            else    MessageBoxA(hwnd,
                                "Device not found or model mismatch.\n"
                                "Connect TAB-A05-BD in fastboot mode.",
                                "Check Failed", MB_ICONERROR);
        } else {
            g_DeviceVerified = false;
            EnableWindow(hBtnFlash, FALSE);
            if (ok) MessageBoxA(hwnd,
                                "All partitions written successfully.",
                                "Flash Done", MB_ICONINFORMATION);
            else    MessageBoxA(hwnd,
                                "Flash failed. See log for details.",
                                "Flash Error", MB_ICONERROR);
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    WNDCLASSA wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "A05BDFlasher";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassA(&wc);

    g_hMain = CreateWindowA("A05BDFlasher", "a05bd Flasher v1.0",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            CW_USEDEFAULT, CW_USEDEFAULT, 504, 390,
                            nullptr, nullptr, hInst, nullptr);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return static_cast<int>(msg.wParam);
}
