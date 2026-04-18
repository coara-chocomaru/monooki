#include "app.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <thread>

namespace {
struct Handle {
    HANDLE h{nullptr};
    Handle() = default;
    explicit Handle(HANDLE v) : h(v) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : h(other.h) { other.h = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            h = other.h;
            other.h = nullptr;
        }
        return *this;
    }
    ~Handle() { reset(); }
    HANDLE get() const noexcept { return h; }
    void reset(HANDLE v = nullptr) noexcept {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = v;
    }
    explicit operator bool() const noexcept { return h && h != INVALID_HANDLE_VALUE; }
};
}

void SafeDeleteObject(HGDIOBJ obj) {
    if (obj) DeleteObject(obj);
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0);
    if (n > 0) {
        std::wstring w(static_cast<size_t>(n - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    }
    n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::wstring Win32ErrorText(DWORD err) {
    if (!err) return L"";
    LPWSTR buffer = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring out;
    if (len && buffer) {
        out.assign(buffer, buffer + len);
        while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' ' || out.back() == L'\t')) {
            out.pop_back();
        }
    }
    if (buffer) LocalFree(buffer);
    if (out.empty()) {
        out = L"Win32 error " + std::to_wstring(err);
    }
    return out;
}

bool FileExistsA(const std::string& path) {
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void AppendLog(const wchar_t* text) {
    if (!hLog || !text) return;
    int i = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, static_cast<WPARAM>(i), static_cast<LPARAM>(i));
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text));
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"\r\n"));
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}

void PostLog(const std::wstring& msg) {
    auto p = std::make_unique<wchar_t[]>(msg.size() + 1);
    wmemcpy(p.get(), msg.c_str(), msg.size() + 1);
    if (!PostMessageW(g_hMain, WM_LOG_POST, 0, reinterpret_cast<LPARAM>(p.get()))) {
        return;
    }
    p.release();
}

void PostText(UINT kind, const std::wstring& msg) {
    auto p = std::make_unique<wchar_t[]>(msg.size() + 1);
    wmemcpy(p.get(), msg.c_str(), msg.size() + 1);
    if (!PostMessageW(g_hMain, WM_TEXT_SET, kind, reinterpret_cast<LPARAM>(p.get()))) {
        return;
    }
    p.release();
}

std::string RomDir() {
    return "TAB-A05-BD";
}

std::string FASTBOOT_EXE() {
    return ".\\platform-tools\\fastboot.exe";
}

std::string FB(const std::string& args) {
    return std::string("\"") + FASTBOOT_EXE() + "\" " + args;
}

std::string AssetPath(const char* filename) {
    return RomDir() + "\\" + filename;
}

std::string Img(const char* filename) {
    return std::string("\"") + AssetPath(filename) + "\"";
}

ExecResult Exec(const std::string& cmdLine) {
    ExecResult r{1, {}, {}};

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        r.launchError = Win32ErrorText(GetLastError());
        return r;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    Handle readHandle(hRead);
    Handle writeHandle(hWrite);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = writeHandle.get();
    si.hStdError = writeHandle.get();
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<char> cmd(cmdLine.begin(), cmdLine.end());
    cmd.push_back('\0');

    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        r.launchError = Win32ErrorText(GetLastError());
        return r;
    }

    Handle proc(pi.hProcess);
    Handle thread(pi.hThread);
    writeHandle.reset();

    std::string out;
    out.reserve(4096);
    char buf[4096];
    DWORD n = 0;
    for (;;) {
        BOOL ok = ReadFile(readHandle.get(), buf, static_cast<DWORD>(sizeof(buf)), &n, nullptr);
        if (!ok || n == 0) break;
        out.append(buf, buf + n);
    }

    WaitForSingleObject(proc.get(), INFINITE);
    GetExitCodeProcess(proc.get(), &r.exitCode);
    r.output = std::move(out);
    return r;
}

HFONT MakeFont(const wchar_t* face, int size, int weight, BOOL italic) {
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    return CreateFontW(-MulDiv(size, dpi, 72), 0, 0, 0, weight, italic, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
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

void PostDone(bool ok, bool flashMode) {
    PostMessageW(g_hMain, WM_OP_DONE, ok ? 1 : 0, flashMode ? 1 : 0);
}

std::vector<FlashStep> BuildFlashSteps() {
    std::vector<FlashStep> steps;
    steps.reserve(33);
    steps.push_back({FB("-w"), L"0/34  Wipe 実行中…", nullptr});
    steps.push_back({FB("flash nvram " + Img("nvranm.img")), L"1/34  NVRAM 書き込み中…", "nvranm.img"});
    steps.push_back({FB("flash nvcfg " + Img("nvcfg.img")), L"2/34  NVCFG 書き込み中…", "nvcfg.img"});
    steps.push_back({FB("flash nvdata " + Img("nvdata.img")), L"3/34  NVDATA 書き込み中…", "nvdata.img"});
    steps.push_back({FB("flash persist " + Img("persist.img")), L"4/34  PERSIST 書き込み中…", "persist.img"});
    steps.push_back({FB("flash preloader " + Img("preloader.img")), L"5/34  PRELOADER 書き込み中…", "preloader.img"});
    steps.push_back({FB("flash partition " + Img("pgpt.img")), L"6/34  PGPT 書き込み中…", "pgpt.img"});
    steps.push_back({FB("flash boot_para " + Img("boot_para.img")), L"7/34  BOOT_PARA 書き込み中…", "boot_para.img"});
    steps.push_back({FB("flash cam_vpu1 " + Img("cam_vpu1.img")), L"8/34  CAM_VPU1 書き込み中…", "cam_vpu1.img"});
    steps.push_back({FB("flash cam_vpu2 " + Img("cam_vpu2.img")), L"9/34  CAM_VPU2 書き込み中…", "cam_vpu2.img"});
    steps.push_back({FB("flash cam_vpu3 " + Img("cam_vpu3.img")), L"10/34  CAM_VPU3 書き込み中…", "cam_vpu3.img"});
    steps.push_back({FB("erase protect1"), L"11/34  PROTECT1 消去中…", nullptr});
    steps.push_back({FB("erase protect2"), L"12/34  PROTECT2 消去中…", nullptr});
    steps.push_back({FB("flash nvram " + Img("nvram.img")), L"13/34  NVRAM 書き込み中…", "nvram.img"});
    steps.push_back({FB("flash lk " + Img("lk.img")), L"14/34  LK 書き込み中…", "lk.img"});
    steps.push_back({FB("flash lk2 " + Img("lk2.img")), L"15/34  LK2 書き込み中…", "lk2.img"});
    steps.push_back({FB("flash boot " + Img("boot.img")), L"16/34  BOOT 書き込み中…", "boot.img"});
    steps.push_back({FB("flash recovery " + Img("recovery.img")), L"17/34  RECOVERY 書き込み中…", "recovery.img"});
    steps.push_back({FB("flash logo " + Img("logo.img")), L"18/34  LOGO 書き込み中…", "logo.img"});
    steps.push_back({FB("flash dtbo " + Img("dtbo.img")), L"19/34  DTBO 書き込み中…", "dtbo.img"});
    steps.push_back({FB("erase expdb"), L"20/34  EXPDB 消去中…", nullptr});
    steps.push_back({FB("flash frp " + Img("frp.img")), L"21/34  FRP 書き込み中…", "frp.img"});
    steps.push_back({FB("erase para"), L"22/34  PARA 消去中…", nullptr});
    steps.push_back({FB("flash tee1 " + Img("tee.img")), L"23/34  TEE1 書き込み中…", "tee.img"});
    steps.push_back({FB("flash tee2 " + Img("tee.img")), L"24/34  TEE2 書き込み中…", "tee.img"});
    steps.push_back({FB("erase kb"), L"25/34  KB 消去中…", nullptr});
    steps.push_back({FB("erase dkb"), L"26/34  DKB 消去中…", nullptr});
    steps.push_back({FB("erase metadata"), L"27/34  METADATA 消去中…", nullptr});
    steps.push_back({FB("flash vbmeta " + Img("vbmeta.img")), L"28/34  VBMETA 書き込み中…", "vbmeta.img"});
    steps.push_back({FB("flash system " + Img("system.img")), L"29/34  SYSTEM 書き込み中…", "system.img"});
    steps.push_back({FB("flash vendor " + Img("vendor.img")), L"30/34  VENDOR 書き込み中…", "vendor.img"});
    steps.push_back({FB("flash factory " + Img("factory.img")), L"31/34  FACTORY 書き込み中…", "factory.img"});
    steps.push_back({FB("flash cache " + Img("cache.img")), L"32/34  CACHE 書き込み中…", "cache.img"});
    return steps;
}

void CheckThread() {
    PostText(0, L"端末確認中");
    PostText(1, L"検出中");
    PostText(2, L"fastboot の応答を確認しています。");
    PostLog(L"━━ 端末確認 ━━");

    if (!FileExistsA(FASTBOOT_EXE())) {
        PostLog(L"エラー: fastboot.exe が見つかりません。");
        PostLog(L"配置先: .\\platform-tools\\fastboot.exe");
        PostDone(false, false);
        g_Busy = false;
        return;
    }

    PostLog(L"fastboot.exe を起動します…");
    auto d = Exec(FB("devices"));
    if (!d.launchError.empty()) {
        PostLog(L"エラー: fastboot.exe の起動に失敗しました。");
        PostLog(L"原因: " + d.launchError);
        PostDone(false, false);
        g_Busy = false;
        return;
    }

    std::string dl = d.output;
    std::transform(dl.begin(), dl.end(), dl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (dl.find("fastboot") == std::string::npos) {
        PostLog(L"エラー: fastboot モードの端末が検出されません。");
        if (!d.output.empty()) PostLog(L"返答: " + ToWide(d.output));
        PostDone(false, false);
        g_Busy = false;
        return;
    }

    PostLog(L"端末を検出しました。product を確認しています…");
    auto v = Exec(FB("getvar product"));
    if (!v.launchError.empty()) {
        PostLog(L"エラー: product 取得に失敗しました。");
        PostLog(L"原因: " + v.launchError);
        PostDone(false, false);
        g_Busy = false;
        return;
    }

    std::string vl = v.output;
    std::transform(vl.begin(), vl.end(), vl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (vl.find("a05bd") == std::string::npos) {
        g_DeviceVerified = false;
        PostLog(L"エラー: モデル不一致。期待値: a05bd");
        if (!v.output.empty()) PostLog(L"返答: " + ToWide(v.output));
        PostDone(false, false);
        g_Busy = false;
        return;
    }

    PostLog(L"端末を確認しました。unlocked を確認しています…");
    auto u = Exec(FB("getvar unlocked"));
    if (!u.launchError.empty()) {
        PostLog(L"エラー: unlocked 取得に失敗しました。");
        PostLog(L"原因: " + u.launchError);
        PostDone(false, false);
        g_Busy = false;
        return;
    }

    std::string ul = u.output;
    std::transform(ul.begin(), ul.end(), ul.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    bool unlocked = (ul.find("yes") != std::string::npos);
    if (!unlocked) {
        g_DeviceVerified = false;
        g_Unlocked = false;
        PostLog(L"エラー: unlocked が yes ではありません。");
        if (!u.output.empty()) PostLog(L"返答: " + ToWide(u.output));
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

void FlashThread() {
    PostText(0, L"書き込み中");
    PostText(2, L"書き込み処理を実行しています。");
    PostLog(L"━━ 書き込み開始 ━━");

    if (!FileExistsA(FASTBOOT_EXE())) {
        PostLog(L"エラー: fastboot.exe が見つかりません。");
        PostDone(false, true);
        g_Busy = false;
        return;
    }

    auto steps = BuildFlashSteps();
    for (const auto& step : steps) {
        if (step.asset && !FileExistsA(AssetPath(step.asset))) {
            PostLog(L"エラー: 画像ファイルが見つかりません。");
            PostLog(L"不足: " + ToWide(AssetPath(step.asset)));
            PostDone(false, true);
            g_Busy = false;
            return;
        }
    }

    PostMessageW(g_hMain, WM_PROG_SET, MAKEWPARAM(0, static_cast<WORD>(steps.size() + 1)), 0);

    for (size_t i = 0; i < steps.size(); ++i) {
        PostLog(steps[i].desc);
        auto r = Exec(steps[i].cmd);
        if (!r.launchError.empty()) {
            PostLog(L"失敗  起動エラー");
            PostLog(L"原因: " + r.launchError);
            PostDone(false, true);
            g_Busy = false;
            return;
        }
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
    if (!end.launchError.empty()) {
        PostLog(L"失敗  起動エラー");
        PostLog(L"原因: " + end.launchError);
        PostDone(false, true);
        g_Busy = false;
        return;
    }
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
