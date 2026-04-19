#include "app.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <string_view>
#include <vector>

namespace {
constexpr size_t kMaxQueuedLines = 384;
constexpr size_t kMaxLogChars = 48000;

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
        if (h && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
        h = v;
    }
};

std::wstring NormalizeForDisplay(const std::wstring& src) {
    std::wstring out;
    out.reserve(src.size());
    for (wchar_t ch : src) {
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n') {
            if (out.empty() || out.back() != L'\n') {
                out.push_back(L'\n');
            }
            continue;
        }
        if (ch < 0x20 && ch != L'\t') {
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

std::vector<std::wstring> SplitLogLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    lines.reserve(1);
    std::wstring current;
    current.reserve(text.size());

    for (wchar_t ch : text) {
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        if (ch < 0x20 && ch != L'\t') {
            continue;
        }
        current.push_back(ch);
    }
    lines.push_back(current);
    return lines;
}

bool ContainsInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) {
            return static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a))) ==
                   static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b)));
        });
    return it != haystack.end();
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
    const int need = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(need - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), need, nullptr, nullptr);
    return out;
}

bool EnsureDirectoryW(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    if (CreateDirectoryW(path.c_str(), nullptr)) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring LogDirectoryW() {
    return ModuleDirW() + L"\\log";
}

std::wstring TimestampForFileW() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[32]{};
    swprintf(buf, 32, L"%04u%02u%02u_%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring BuildLogPathW(const wchar_t* suffix) {
    const std::wstring dir = LogDirectoryW();
    EnsureDirectoryW(dir);
    return dir + L"\\" + TimestampForFileW() + L"_" + suffix + L".txt";
}

bool WriteUtf8TextFileW(const std::wstring& path, const std::wstring& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    Handle file(h);
    const std::string utf8 = WideToUtf8(text);
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    if (!WriteFile(file.get(), bom, 3, &written, nullptr)) {
        return false;
    }
    if (!utf8.empty()) {
        if (!WriteFile(file.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
            return false;
        }
    }
    return true;
}

struct SessionLog {
    uint32_t token{};
    std::wstring body;

    void add(const std::wstring& msg) {
        QueueLog(token, msg);
        const auto lines = SplitLogLines(NormalizeForDisplay(msg));
        for (const auto& line : lines) {
            body.append(line);
            body.append(L"\r\n");
        }
    }

    void save(const wchar_t* suffix, bool ok, bool flashMode) const {
        if (!g_LogEnabled.load(std::memory_order_acquire)) {
            return;
        }

        std::wstring out;
        out.reserve(body.size() + 128);
        out.append(L"日時: ");
        out.append(TimestampForFileW());
        out.append(L"\r\n");
        out.append(L"種別: ");
        out.append(flashMode ? L"ROM書き込み" : L"端末確認");
        out.append(L"\r\n");
        out.append(L"結果: ");
        out.append(ok ? L"成功" : L"失敗");
        out.append(L"\r\n\r\n");
        out.append(body);
        WriteUtf8TextFileW(BuildLogPathW(suffix), out);
    }
};

void TrimLogWindow(size_t limitChars = kMaxLogChars) {
    if (!hLog) {
        return;
    }
    const int len = GetWindowTextLengthW(hLog);
    if (len <= static_cast<int>(limitChars)) {
        return;
    }
    const int removeChars = len - static_cast<int>(limitChars);
    SendMessageW(hLog, EM_SETSEL, 0, removeChars);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
}

} // namespace

std::wstring ModuleDirW() {
    static const std::wstring cached = [] {
        wchar_t buffer[MAX_PATH * 4]{};
        const DWORD len = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
        if (len == 0 || len >= (sizeof(buffer) / sizeof(buffer[0]))) {
            return std::wstring(L".");
        }
        std::wstring path(buffer, buffer + len);
        const size_t pos = path.find_last_of(L"\\/");
        if (pos == std::wstring::npos) {
            return std::wstring(L".");
        }
        path.resize(pos);
        return path;
    }();
    return cached;
}

std::string WideToAnsi(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
    const int need = WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(need - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring ResolveAppPathW(const std::wstring& path) {
    if (path.empty()) {
        return {};
    }

    std::wstring p = path;
    std::replace(p.begin(), p.end(), L'/', L'\\');

    if (p.size() >= 2 && p[1] == L':') {
        return p;
    }
    if (p.rfind(L"\\\\", 0) == 0) {
        return p;
    }
    if (!p.empty() && p.front() == L'\\') {
        return p;
    }
    while (p.rfind(L".\\", 0) == 0) {
        p.erase(0, 2);
    }
    if (p.empty()) {
        return ModuleDirW();
    }
    return ModuleDirW() + L"\\" + p;
}

void SafeDeleteObject(HGDIOBJ obj) {
    if (obj) {
        DeleteObject(obj);
    }
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) {
        return {};
    }

    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0);
    if (n > 0) {
        std::wstring w(static_cast<size_t>(n - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    }

    n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) {
        return {};
    }

    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::wstring Win32ErrorText(DWORD err) {
    if (!err) {
        return L"";
    }

    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(flags, nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring out;
    if (len && buffer) {
        out.assign(buffer, buffer + len);
        while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' ' || out.back() == L'\t')) {
            out.pop_back();
        }
    }
    if (buffer) {
        LocalFree(buffer);
    }
    if (out.empty()) {
        out = L"Win32 error " + std::to_wstring(err);
    }
    return out;
}

bool FileExistsA(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string RomDir() {
    const std::wstring raw = g_ConfigRomDir.empty() ? L"./TAB-A05-BD" : g_ConfigRomDir;
    return WideToAnsi(ResolveAppPathW(raw));
}

std::string FASTBOOT_EXE() {
    return WideToAnsi(ModuleDirW() + L"\\platform-tools\\fastboot.exe");
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
    ExecResult r{};
    r.exitCode = 1;

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
        const BOOL ok = ReadFile(readHandle.get(), buf, static_cast<DWORD>(sizeof(buf)), &n, nullptr);
        if (!ok || n == 0) {
            break;
        }
        out.append(buf, buf + n);
    }

    WaitForSingleObject(proc.get(), INFINITE);
    GetExitCodeProcess(proc.get(), &r.exitCode);
    r.output = std::move(out);
    return r;
}

HFONT MakeFont(const wchar_t* face, int size, int weight, BOOL italic) {
    HDC hdc = GetDC(nullptr);
    const int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    return CreateFontW(-MulDiv(size, dpi, 72), 0, 0, 0, weight, italic, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

uint32_t BeginOperation() {
    const uint32_t token = g_CurrentOperationToken.fetch_add(1, std::memory_order_acq_rel) + 1;
    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        g_LogQueue.clear();
    }
    g_LogFlushPending.store(false, std::memory_order_release);
    return token;
}

void QueueLog(uint32_t token, const std::wstring& msg) {
    const auto lines = SplitLogLines(NormalizeForDisplay(msg));

    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        for (const auto& line : lines) {
            if (g_LogQueue.size() >= kMaxQueuedLines) {
                g_LogQueue.pop_front();
            }
            g_LogQueue.push_back(LogLine{token, line});
        }
    }

    bool expected = false;
    if (g_LogFlushPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        if (!PostMessageW(g_hMain, WM_LOG_FLUSH, 0, 0)) {
            g_LogFlushPending.store(false, std::memory_order_release);
        }
    }
}

void QueueText(uint32_t token, UINT kind, const std::wstring& msg) {
    auto* p = new TextMessage{};
    p->token = token;
    p->kind = kind;
    p->text = msg;
    if (!PostMessageW(g_hMain, WM_TEXT_SET, reinterpret_cast<WPARAM>(p), 0)) {
        delete p;
    }
}

void QueueProgress(uint32_t token, WORD pos, WORD rng) {
    PostMessageW(g_hMain, WM_PROG_SET, static_cast<WPARAM>(token), MAKELPARAM(pos, rng));
}

void QueueDone(uint32_t token, bool ok, bool flashMode) {
    const LPARAM flags = (ok ? 1 : 0) | (flashMode ? 2 : 0);
    PostMessageW(g_hMain, WM_OP_DONE, static_cast<WPARAM>(token), flags);
}

void UpdateText(HWND hwnd, const std::wstring& text) {
    if (hwnd) {
        SetWindowTextW(hwnd, text.c_str());
    }
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

void AppendLogBlock(const std::wstring& text) {
    if (!hLog || text.empty()) {
        return;
    }

    SendMessageW(hLog, WM_SETREDRAW, FALSE, 0);
    const int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    TrimLogWindow();
    SendMessageW(hLog, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hLog, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}

void FlushLogQueue() {
    std::deque<LogLine> items;
    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        items.swap(g_LogQueue);
    }
    g_LogFlushPending.store(false, std::memory_order_release);

    if (items.empty()) {
        return;
    }

    const uint32_t current = g_CurrentOperationToken.load(std::memory_order_acquire);
    std::wstring batch;
    batch.reserve(2048);

    for (const auto& item : items) {
        if (item.token != current) {
            continue;
        }
        if (!item.text.empty()) {
            batch.append(item.text);
        }
        batch.append(L"\r\n");
    }

    if (!batch.empty()) {
        AppendLogBlock(batch);
    }
}

void DrawRoundCard(HDC hdc, const RECT& rc, COLORREF fill, COLORREF edge, int radius) {
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
    RECT rc{x, y, x + w, y + h};
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
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawButtonFace(LPDRAWITEMSTRUCT dis, bool hot, bool pressed, bool enabled) {
    RECT rc = dis->rcItem;
    const COLORREF fill = enabled ? (pressed ? C_BTN_DN : (hot ? C_BTN_HOV : C_BTN)) : RGB(33, 39, 54);
    const COLORREF edge = enabled ? C_BTN_EDGE : RGB(68, 78, 103);

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
    DrawTextW(dis->hDC, text, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintMain(HDC hdc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    DrawBackgroundImage(hdc, rc);

    RECT header = {rc.left, rc.top, rc.right, rc.top + 124};
    HBRUSH headerBr = CreateSolidBrush(C_BG2);
    FillRect(hdc, &header, headerBr);
    DeleteObject(headerBr);

    DrawRoundCard(hdc, g_layout.leftCard, C_PANEL, C_LINE);
    DrawRoundCard(hdc, g_layout.rightCard, C_PANEL, C_LINE);
    DrawRoundCard(hdc, g_layout.logCard, C_PANEL2, C_LINE);

    RECT accent = {rc.left + 18, 122, rc.right - 18, 124};
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

    DrawChip(hdc, rc.right - 322, 24, 92, 26, RGB(34, 44, 68), C_LINE, L"v1.0");

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

void CleanupGdi() {
    SafeDeleteObject(g_hFontTitle);
    SafeDeleteObject(g_hFontSub);
    SafeDeleteObject(g_hFontBody);
    SafeDeleteObject(g_hFontMono);
    SafeDeleteObject(g_brPanel);
    SafeDeleteObject(g_brPanel2);
    SafeDeleteObject(g_brEdit);
    CleanupOptions();
}

std::vector<FlashStep> BuildFlashSteps() {
    std::vector<FlashStep> steps;
    steps.reserve(32);

    std::string iniPath = WideToAnsi(ModuleDirW() + L"\\option.ini");
    const std::string romDir = RomDir();
    const auto img = [&](const char* filename) {
        return std::string("\"") + romDir + "\\" + filename + "\"";
    };

    auto opt = [&](const char* key) {
        if (!FileExistsA(iniPath)) return true;
        return GetPrivateProfileIntA("Partitions", key, 1, iniPath.c_str()) != 0;
    };

    if (opt("pgpt")) steps.push_back({FB("flash partition " + img("pgpt.img")), L"PGPT 書き込み中…", "pgpt.img"});
    if (opt("nvcfg")) steps.push_back({FB("flash nvcfg " + img("nvcfg.img")), L"NVCFG 書き込み中…", "nvcfg.img"});
    if (opt("nvdata")) steps.push_back({FB("flash nvdata " + img("nvdata.img")), L"NVDATA 書き込み中…", "nvdata.img"});
    if (opt("persist")) steps.push_back({FB("flash persist " + img("persist.img")), L"PERSIST 書き込み中…", "persist.img"});
    if (opt("preloader")) steps.push_back({FB("flash preloader " + img("preloader.img")), L"preloader書き込み中…", "preloader.img"});
    if (opt("boot_para")) steps.push_back({FB("flash boot_para " + img("boot_para.img")), L"BOOT_PARA 書き込み中…", "boot_para.img"});
    if (opt("cam_vpu1")) steps.push_back({FB("flash cam_vpu1 " + img("cam_vpu1.img")), L"CAM_VPU1 書き込み中…", "cam_vpu1.img"});
    if (opt("cam_vpu2")) steps.push_back({FB("flash cam_vpu2 " + img("cam_vpu2.img")), L"CAM_VPU2 書き込み中…", "cam_vpu2.img"});
    if (opt("cam_vpu3")) steps.push_back({FB("flash cam_vpu3 " + img("cam_vpu3.img")), L"CAM_VPU3 書き込み中…", "cam_vpu3.img"});
    if (opt("protect1_erase")) steps.push_back({FB("erase protect1"), L"PROTECT1 消去中…", nullptr});
    if (opt("protect2_erase")) steps.push_back({FB("erase protect2"), L"PROTECT2 消去中…", nullptr});
    if (opt("nvram_erase")) steps.push_back({FB("erase nvram"), L"NVRAM 消去中…", nullptr});
    if (opt("nvram")) steps.push_back({FB("flash nvram " + img("nvram.img")), L"NVRAM 書き込み中…", "nvram.img"});
    if (opt("lk")) steps.push_back({FB("flash lk " + img("lk.img")), L"LK 書き込み中…", "lk.img"});
    if (opt("lk2")) steps.push_back({FB("flash lk2 " + img("lk2.img")), L"LK2 書き込み中…", "lk2.img"});
    if (opt("boot")) steps.push_back({FB("flash boot " + img("boot.img")), L"BOOT 書き込み中…", "boot.img"});
    if (opt("recovery")) steps.push_back({FB("flash recovery " + img("recovery.img")), L"RECOVERY 書き込み中…", "recovery.img"});
    if (opt("logo")) steps.push_back({FB("flash logo " + img("logo.img")), L"LOGO 書き込み中…", "logo.img"});
    if (opt("dtbo")) steps.push_back({FB("flash dtbo " + img("dtbo.img")), L"DTBO 書き込み中…", "dtbo.img"});
    if (opt("expdb_erase")) steps.push_back({FB("erase expdb"), L"EXPDB 消去中…", nullptr});
    if (opt("frp")) steps.push_back({FB("flash frp " + img("frp.img")), L"FRP 書き込み中…", "frp.img"});
    if (opt("para_erase")) steps.push_back({FB("erase para"), L"PARA 消去中…", nullptr});
    if (opt("tee1")) steps.push_back({FB("flash tee1 " + img("tee.img")), L"TEE1 書き込み中…", "tee.img"});
    if (opt("tee2")) steps.push_back({FB("flash tee2 " + img("tee.img")), L"TEE2 書き込み中…", "tee.img"});
    if (opt("kb_erase")) steps.push_back({FB("erase kb"), L"KB 消去中…", nullptr});
    if (opt("dkb_erase")) steps.push_back({FB("erase dkb"), L"DKB 消去中…", nullptr});
    if (opt("metadata_erase")) steps.push_back({FB("erase metadata"), L"METADATA 消去中…", nullptr});
    if (opt("vbmeta")) steps.push_back({FB("flash vbmeta " + img("vbmeta.img")), L"VBMETA 書き込み中…", "vbmeta.img"});
    if (opt("system")) steps.push_back({FB("flash system " + img("system.img")), L"SYSTEM 書き込み中…", "system.img"});
    if (opt("vendor")) steps.push_back({FB("flash vendor " + img("vendor.img")), L"VENDOR 書き込み中…", "vendor.img"});
    if (opt("factory")) steps.push_back({FB("flash factory " + img("factory.img")), L"FACTORY 書き込み中…", "factory.img"});
    if (opt("cache")) steps.push_back({FB("flash cache " + img("cache.img")), L"CACHE 書き込み中…", "cache.img"});

    return steps;
}

void CheckThread(uint32_t token) {
    SessionLog log{token};
    const std::string fastboot = FASTBOOT_EXE();
    auto finish = [&](bool ok) {
        log.save(L"端末確認ログ", ok, false);
        QueueDone(token, ok, false);
    };

    QueueText(token, 0, L"端末確認中");
    QueueText(token, 1, L"検出中");
    QueueText(token, 2, L"fastboot の応答を確認しています。");
    log.add(L"━━ 端末確認 ━━");

    if (!FileExistsA(fastboot)) {
        log.add(L"エラー: fastboot.exe が見つかりません。");
        log.add(L"配置先: .\\platform-tools\\fastboot.exe");
        finish(false);
        return;
    }

    log.add(L"fastboot.exe を起動します…");
    auto d = Exec(FB("devices"));
    if (!d.launchError.empty()) {
        log.add(L"エラー: fastboot.exe の起動に失敗しました。");
        log.add(L"原因: " + d.launchError);
        finish(false);
        return;
    }

    if (!d.output.empty()) {
        log.add(L"応答: " + ToWide(d.output));
    }

    if (!ContainsInsensitive(d.output, "fastboot")) {
        log.add(L"エラー: fastboot モードの端末が検出されません。");
        finish(false);
        return;
    }

    log.add(L"端末を検出しました。product を確認しています…");
    auto v = Exec(FB("getvar product"));
    if (!v.launchError.empty()) {
        log.add(L"エラー: product 取得に失敗しました。");
        log.add(L"原因: " + v.launchError);
        finish(false);
        return;
    }

    if (!v.output.empty()) {
        log.add(L"product 応答: " + ToWide(v.output));
    }

    if (!ContainsInsensitive(v.output, "a05bd")) {
        log.add(L"エラー: モデル不一致。期待値: a05bd");
        finish(false);
        return;
    }

    log.add(L"端末を確認しました。unlocked を確認しています…");
    auto u = Exec(FB("getvar unlocked"));
    if (!u.launchError.empty()) {
        log.add(L"エラー: unlocked 取得に失敗しました。");
        log.add(L"原因: " + u.launchError);
        finish(false);
        return;
    }

    if (!u.output.empty()) {
        log.add(L"unlocked 応答: " + ToWide(u.output));
    }

    const bool unlocked = ContainsInsensitive(u.output, "yes");
    if (!unlocked) {
        log.add(L"エラー: unlocked が yes ではありません。");
        log.add(L"先にアンロックをしてください！");
        finish(false);
        return;
    }

    log.add(L"確認済み: a05bd");
    log.add(L"unlocked: yes");
    finish(true);
}

void FlashThread(uint32_t token) {
    SessionLog log{token};
    const std::string fastboot = FASTBOOT_EXE();
    auto finish = [&](bool ok) {
        log.save(L"rom書き込みログ", ok, true);
        QueueDone(token, ok, true);
    };

    QueueText(token, 0, L"書き込み中");
    QueueText(token, 2, L"書き込み処理を実行しています。");
    log.add(L"━━ 書き込み開始 ━━");

    if (!FileExistsA(fastboot)) {
        log.add(L"エラー: fastboot.exe が見つかりません。");
        finish(false);
        return;
    }

    std::string iniPath = WideToAnsi(ModuleDirW() + L"\\option.ini");
    auto GetOpt = [&](const char* key) {
        if (!FileExistsA(iniPath)) return true;
        return GetPrivateProfileIntA("Partitions", key, 1, iniPath.c_str()) != 0;
    };

    const auto steps = BuildFlashSteps();
    const bool doReboot = GetOpt("reboot");

    int totalSteps = static_cast<int>(steps.size()) + (doReboot ? 1 : 0);
    QueueProgress(token, 0, static_cast<WORD>(totalSteps));

    for (size_t i = 0; i < steps.size(); ++i) {
        std::wstring prefix = std::to_wstring(i) + L"/" + std::to_wstring(totalSteps) + L"  ";
        log.add(prefix + steps[i].desc);
        auto r = Exec(steps[i].cmd);
        if (!r.launchError.empty()) {
            log.add(L"失敗  起動エラー");
            log.add(L"原因: " + r.launchError);
            finish(false);
            return;
        }
        if (r.exitCode != 0) {
            log.add(L"失敗  終了コード=" + std::to_wstring(r.exitCode));
            if (!r.output.empty()) {
                log.add(L"出力: " + ToWide(r.output));
            }
            finish(false);
            return;
        }
        QueueProgress(token, static_cast<WORD>(i + 1), static_cast<WORD>(totalSteps));
    }

    if (doReboot) {
        std::wstring finalPrefix = std::to_wstring(steps.size()) + L"/" + std::to_wstring(totalSteps) + L"  ";
        log.add(finalPrefix + L"最終処理: reboot-recovery");
        auto end = Exec(FB("oem reboot-recovery"));
        if (!end.launchError.empty()) {
            log.add(L"失敗  起動エラー");
            log.add(L"原因: " + end.launchError);
            finish(false);
            return;
        }
        if (end.exitCode != 0) {
            log.add(L"失敗  終了コード=" + std::to_wstring(end.exitCode));
            if (!end.output.empty()) {
                log.add(L"出力: " + ToWide(end.output));
            }
            finish(false);
            return;
        }
        QueueProgress(token, static_cast<WORD>(totalSteps), static_cast<WORD>(totalSteps));
    } else {
        log.add(L"最終処理: スキップされました (option.ini)");
    }

    log.add(L"==============================");
    log.add(L"すべての書き込みに成功しました。");
    finish(true);
}
