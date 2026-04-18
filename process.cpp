#include "app.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

namespace {
constexpr size_t kMaxQueuedLines = 384;
constexpr size_t kMaxLogChars = 48000;

struct IniStepFlags {
    bool flash{true};
    bool erase{true};
    bool img{true};
};

struct IniConfig {
    std::map<std::wstring, std::map<std::wstring, bool>> sections;
};

std::wstring TrimW(std::wstring s) {
    size_t begin = 0;
    while (begin < s.size() && (s[begin] == L' ' || s[begin] == L'\t' || s[begin] == L'\r' || s[begin] == L'\n')) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' || s[end - 1] == L'\n')) {
        --end;
    }
    s.erase(end);
    s.erase(0, begin);
    return s;
}

std::wstring LowerW(std::wstring s) {
    for (wchar_t& ch : s) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return s;
}

std::wstring NormalizeW(std::wstring s) {
    return LowerW(TrimW(std::move(s)));
}

std::wstring StripInlineComment(std::wstring s) {
    const size_t pos = s.find_first_of(L";#");
    if (pos != std::wstring::npos) {
        s.erase(pos);
    }
    return TrimW(std::move(s));
}

bool ParseBoolW(const std::wstring& text, bool fallback = true) {
    const std::wstring v = NormalizeW(text);
    if (v.empty()) {
        return fallback;
    }
    if (v == L"1" || v == L"true" || v == L"yes" || v == L"on" || v == L"enable" || v == L"enabled") {
        return true;
    }
    if (v == L"0" || v == L"false" || v == L"no" || v == L"off" || v == L"disable" || v == L"disabled") {
        return false;
    }
    return fallback;
}

bool FileExistsW(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring ModuleDirW();

std::wstring OptionIniPathW() {
    return ModuleDirW() + L"\option.ini";
}

std::wstring ReadTextFileW(const std::wstring& path) {
    if (!FileExistsW(path)) {
        return {};
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(file, &sz) || sz.QuadPart <= 0) {
        CloseHandle(file);
        return {};
    }

    if (sz.QuadPart > static_cast<LONGLONG>(std::numeric_limits<size_t>::max())) {
        CloseHandle(file);
        return {};
    }

    const size_t size = static_cast<size_t>(sz.QuadPart);
    std::string bytes(size, '\0');
    DWORD total = 0;
    while (total < size) {
        DWORD chunk = 0;
        if (!ReadFile(file, bytes.data() + total, static_cast<DWORD>(size - total), &chunk, nullptr) || chunk == 0) {
            break;
        }
        total += chunk;
    }
    CloseHandle(file);
    bytes.resize(total);

    if (bytes.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(bytes[0]);
        const unsigned char b1 = static_cast<unsigned char>(bytes[1]);
        if (b0 == 0xFF && b1 == 0xFE) {
            const size_t wcharCount = (bytes.size() - 2) / 2;
            std::wstring out(wcharCount, L'\0');
            std::memcpy(out.data(), bytes.data() + 2, wcharCount * sizeof(wchar_t));
            return out;
        }
        if (b0 == 0xFE && b1 == 0xFF) {
            std::wstring out;
            out.reserve((bytes.size() - 2) / 2);
            for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
                const wchar_t ch = static_cast<wchar_t>((static_cast<unsigned char>(bytes[i]) << 8) |
                                                        static_cast<unsigned char>(bytes[i + 1]));
                out.push_back(ch);
            }
            return out;
        }
    }

    return ToWide(bytes);
}

IniConfig LoadOptionConfig() {
    IniConfig cfg{};
    const std::wstring path = OptionIniPathW();
    if (!FileExistsW(path)) {
        return cfg;
    }

    const std::wstring text = ReadTextFileW(path);
    if (text.empty()) {
        return cfg;
    }

    std::wstring section = L"default";
    const auto lines = SplitLogLines(text);
    for (const auto& rawLine : lines) {
        std::wstring line = TrimW(rawLine);
        if (line.empty()) {
            continue;
        }
        if (line[0] == L';' || line[0] == L'#') {
            continue;
        }
        if (line.front() == L'[' && line.back() == L']' && line.size() >= 2) {
            section = NormalizeW(line.substr(1, line.size() - 2));
            if (section.empty() || section == L"default" || section == L"global" || section == L"*") {
                section = L"default";
            }
            continue;
        }

        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }

        std::wstring key = NormalizeW(line.substr(0, eq));
        std::wstring value = StripInlineComment(line.substr(eq + 1));
        if (key.empty()) {
            continue;
        }
        cfg.sections[section][key] = ParseBoolW(value, true);
    }
    return cfg;
}

IniStepFlags ResolveStepFlags(const IniConfig& cfg, const std::string& partition) {
    IniStepFlags flags{};

    auto apply = [&](const std::wstring& sectionName) {
        const auto sit = cfg.sections.find(sectionName);
        if (sit == cfg.sections.end()) {
            return;
        }
        for (const auto& kv : sit->second) {
            if (kv.first == L"flash") {
                flags.flash = kv.second;
            } else if (kv.first == L"erase") {
                flags.erase = kv.second;
            } else if (kv.first == L"img") {
                flags.img = kv.second;
            }
        }
    };

    apply(L"default");
    apply(NormalizeW(ToWide(partition)));
    return flags;
}

std::wstring StepLabel(const std::string& partition, FlashAction action) {
    const std::wstring p = ToWide(partition);
    if (action == FlashAction::Erase) {
        return L"【" + p + L"】 erase";
    }
    return L"【" + p + L"】 flash";
}


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

namespace {
std::wstring ModuleDirW() {
    wchar_t buffer[MAX_PATH * 4]{};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
    if (len == 0 || len >= (sizeof(buffer) / sizeof(buffer[0]))) {
        return L".";
    }
    std::wstring path(buffer, buffer + len);
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    path.resize(pos);
    return path;
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
} // namespace

bool FileExistsA(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string RomDir() {
    return WideToAnsi(ModuleDirW() + L"\\TAB-A05-BD");
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
    (void)token;
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

void PaintMain(HDC hdc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

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

    DrawChip(hdc, rc.right - 170, 24, 140, 26, RGB(34, 44, 68), C_LINE, L"fastboot utility");

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

void CleanupGdi() {
    SafeDeleteObject(g_hFontTitle);
    SafeDeleteObject(g_hFontSub);
    SafeDeleteObject(g_hFontBody);
    SafeDeleteObject(g_hFontMono);
    SafeDeleteObject(g_brPanel);
    SafeDeleteObject(g_brPanel2);
    SafeDeleteObject(g_brEdit);
}

std::vector<FlashStep> BuildFlashSteps() {
    std::vector<FlashStep> steps;
    steps.reserve(33);
    steps.push_back({"pgpt", FlashAction::Flash, FB("flash partition " + Img("pgpt.img")), L"0/34  PGPT 書き込み中…", "pgpt.img"});
    steps.push_back({"nvram", FlashAction::Flash, FB("flash nvram " + Img("nvram.img")), L"1/34  NVRAM 書き込み中…", "nvram.img"});
    steps.push_back({"nvcfg", FlashAction::Flash, FB("flash nvcfg " + Img("nvcfg.img")), L"2/34  NVCFG 書き込み中…", "nvcfg.img"});
    steps.push_back({"nvdata", FlashAction::Flash, FB("flash nvdata " + Img("nvdata.img")), L"3/34  NVDATA 書き込み中…", "nvdata.img"});
    steps.push_back({"persist", FlashAction::Flash, FB("flash persist " + Img("persist.img")), L"4/34  PERSIST 書き込み中…", "persist.img"});
    steps.push_back({"preloader", FlashAction::Flash, FB("flash preloader " + Img("preloader.img")), L"5/34  preloader書き込み中…", "preloader.img"});
    steps.push_back({"boot_para", FlashAction::Flash, FB("flash boot_para " + Img("boot_para.img")), L"6/34  BOOT_PARA 書き込み中…", "boot_para.img"});
    steps.push_back({"cam_vpu1", FlashAction::Flash, FB("flash cam_vpu1 " + Img("cam_vpu1.img")), L"7/34  CAM_VPU1 書き込み中…", "cam_vpu1.img"});
    steps.push_back({"cam_vpu2", FlashAction::Flash, FB("flash cam_vpu2 " + Img("cam_vpu2.img")), L"8/34  CAM_VPU2 書き込み中…", "cam_vpu2.img"});
    steps.push_back({"cam_vpu3", FlashAction::Flash, FB("flash cam_vpu3 " + Img("cam_vpu3.img")), L"9/34  CAM_VPU3 書き込み中…", "cam_vpu3.img"});
    steps.push_back({"protect1", FlashAction::Erase, FB("erase protect1"), L"10/34  PROTECT1 消去中…", nullptr});
    steps.push_back({"protect2", FlashAction::Erase, FB("erase protect2"), L"11/34  PROTECT2 消去中…", nullptr});
    steps.push_back({"nvram", FlashAction::Erase, FB("erase nvram"), L"12/34  NVRAM 消去中…", nullptr});
    steps.push_back({"nvram", FlashAction::Flash, FB("flash nvram " + Img("nvram.img")), L"13/34  NVRAM 書き込み中…", "nvram.img"});
    steps.push_back({"lk", FlashAction::Flash, FB("flash lk " + Img("lk.img")), L"14/34  LK 書き込み中…", "lk.img"});
    steps.push_back({"lk2", FlashAction::Flash, FB("flash lk2 " + Img("lk2.img")), L"15/34  LK2 書き込み中…", "lk2.img"});
    steps.push_back({"boot", FlashAction::Flash, FB("flash boot " + Img("boot.img")), L"16/34  BOOT 書き込み中…", "boot.img"});
    steps.push_back({"recovery", FlashAction::Flash, FB("flash recovery " + Img("recovery.img")), L"17/34  RECOVERY 書き込み中…", "recovery.img"});
    steps.push_back({"logo", FlashAction::Flash, FB("flash logo " + Img("logo.img")), L"18/34  LOGO 書き込み中…", "logo.img"});
    steps.push_back({"dtbo", FlashAction::Flash, FB("flash dtbo " + Img("dtbo.img")), L"19/34  DTBO 書き込み中…", "dtbo.img"});
    steps.push_back({"expdb", FlashAction::Erase, FB("erase expdb"), L"20/34  EXPDB 消去中…", nullptr});
    steps.push_back({"frp", FlashAction::Flash, FB("flash frp " + Img("frp.img")), L"21/34  FRP 書き込み中…", "frp.img"});
    steps.push_back({"para", FlashAction::Erase, FB("erase para"), L"22/34  PARA 消去中…", nullptr});
    steps.push_back({"tee1", FlashAction::Flash, FB("flash tee1 " + Img("tee.img")), L"23/34  TEE1 書き込み中…", "tee.img"});
    steps.push_back({"tee2", FlashAction::Flash, FB("flash tee2 " + Img("tee.img")), L"24/34  TEE2 書き込み中…", "tee.img"});
    steps.push_back({"kb", FlashAction::Erase, FB("erase kb"), L"25/34  KB 消去中…", nullptr});
    steps.push_back({"dkb", FlashAction::Erase, FB("erase dkb"), L"26/34  DKB 消去中…", nullptr});
    steps.push_back({"metadata", FlashAction::Erase, FB("erase metadata"), L"27/34  METADATA 消去中…", nullptr});
    steps.push_back({"vbmeta", FlashAction::Flash, FB("flash vbmeta " + Img("vbmeta.img")), L"28/34  VBMETA 書き込み中…", "vbmeta.img"});
    steps.push_back({"system", FlashAction::Flash, FB("flash system " + Img("system.img")), L"29/34  SYSTEM 書き込み中…", "system.img"});
    steps.push_back({"vendor", FlashAction::Flash, FB("flash vendor " + Img("vendor.img")), L"30/34  VENDOR 書き込み中…", "vendor.img"});
    steps.push_back({"factory", FlashAction::Flash, FB("flash factory " + Img("factory.img")), L"31/34  FACTORY 書き込み中…", "factory.img"});
    steps.push_back({"cache", FlashAction::Flash, FB("flash cache " + Img("cache.img")), L"32/34  CACHE 書き込み中…", "cache.img"});
    return steps;
}

void CheckThread(uint32_t token) {
    QueueText(token, 0, L"端末確認中");
    QueueText(token, 1, L"検出中");
    QueueText(token, 2, L"fastboot の応答を確認しています。");
    QueueLog(token, L"━━ 端末確認 ━━");

    if (!FileExistsA(FASTBOOT_EXE())) {
        QueueLog(token, L"エラー: fastboot.exe が見つかりません。");
        QueueLog(token, L"配置先: .\\platform-tools\\fastboot.exe");
        QueueDone(token, false, false);
        return;
    }

    QueueLog(token, L"fastboot.exe を起動します…");
    auto d = Exec(FB("devices"));
    if (!d.launchError.empty()) {
        QueueLog(token, L"エラー: fastboot.exe の起動に失敗しました。");
        QueueLog(token, L"原因: " + d.launchError);
        QueueDone(token, false, false);
        return;
    }

    if (!d.output.empty()) {
        QueueLog(token, L"応答: " + ToWide(d.output));
    }

    if (!ContainsInsensitive(d.output, "fastboot")) {
        QueueLog(token, L"エラー: fastboot モードの端末が検出されません。");
        QueueDone(token, false, false);
        return;
    }

    QueueLog(token, L"端末を検出しました。product を確認しています…");
    auto v = Exec(FB("getvar product"));
    if (!v.launchError.empty()) {
        QueueLog(token, L"エラー: product 取得に失敗しました。");
        QueueLog(token, L"原因: " + v.launchError);
        QueueDone(token, false, false);
        return;
    }

    if (!v.output.empty()) {
        QueueLog(token, L"product 応答: " + ToWide(v.output));
    }

    if (!ContainsInsensitive(v.output, "a05bd")) {
        QueueLog(token, L"エラー: モデル不一致。期待値: a05bd");
        QueueDone(token, false, false);
        return;
    }

    QueueLog(token, L"端末を確認しました。unlocked を確認しています…");
    auto u = Exec(FB("getvar unlocked"));
    if (!u.launchError.empty()) {
        QueueLog(token, L"エラー: unlocked 取得に失敗しました。");
        QueueLog(token, L"原因: " + u.launchError);
        QueueDone(token, false, false);
        return;
    }

    if (!u.output.empty()) {
        QueueLog(token, L"unlocked 応答: " + ToWide(u.output));
    }

    const bool unlocked = ContainsInsensitive(u.output, "yes");
    if (!unlocked) {
        QueueLog(token, L"エラー: unlocked が yes ではありません。");
        QueueLog(token, L"先にアンロックをしてください！");
        QueueDone(token, false, false);
        return;
    }

    QueueLog(token, L"確認済み: a05bd");
    QueueLog(token, L"unlocked: yes");
    QueueDone(token, true, false);
}

void FlashThread(uint32_t token) {
    QueueText(token, 0, L"書き込み中");
    QueueText(token, 2, L"書き込み処理を実行しています。");
    QueueLog(token, L"━━ 書き込み開始 ━━");

    if (!FileExistsA(FASTBOOT_EXE())) {
        QueueLog(token, L"エラー: fastboot.exe が見つかりません。");
        QueueDone(token, false, true);
        return;
    }

    const std::wstring iniPath = OptionIniPathW();
    const bool hasIni = FileExistsW(iniPath);
    const IniConfig ini = LoadOptionConfig();
    if (hasIni) {
        QueueLog(token, L"option.ini を検出しました。設定に従って処理を分岐します。");
    } else {
        QueueLog(token, L"option.ini がないため、既存処理のまま進めます。");
    }

    const auto plannedSteps = BuildFlashSteps();
    std::vector<FlashStep> steps;
    steps.reserve(plannedSteps.size());

    for (const auto& step : plannedSteps) {
        const IniStepFlags flags = ResolveStepFlags(ini, step.partition);
        const std::wstring partitionName = ToWide(step.partition);

        if (step.action == FlashAction::Flash && !flags.flash) {
            QueueLog(token, L"スキップ: " + partitionName + L" / flash=false");
            continue;
        }
        if (step.action == FlashAction::Erase && !flags.erase) {
            QueueLog(token, L"スキップ: " + partitionName + L" / erase=false");
            continue;
        }
        steps.push_back(step);
    }

    if (steps.empty()) {
        QueueLog(token, L"実行対象がありません。設定を確認してください。");
        QueueDone(token, true, true);
        return;
    }

    QueueProgress(token, 0, static_cast<WORD>(steps.size() + 1));

    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        const IniStepFlags flags = ResolveStepFlags(ini, step.partition);

        if (step.action == FlashAction::Flash && flags.img && step.asset && !FileExistsA(AssetPath(step.asset))) {
            QueueLog(token, L"エラー: 画像ファイルが見つかりません。");
            QueueLog(token, L"不足: " + ToWide(AssetPath(step.asset)));
            QueueDone(token, false, true);
            return;
        }

        QueueLog(token, step.desc);
        auto r = Exec(step.cmd);
        if (!r.launchError.empty()) {
            QueueLog(token, L"失敗  起動エラー");
            QueueLog(token, L"原因: " + r.launchError);
            QueueDone(token, false, true);
            return;
        }
        if (r.exitCode != 0) {
            QueueLog(token, L"失敗  終了コード=" + std::to_wstring(r.exitCode));
            if (!r.output.empty()) {
                QueueLog(token, L"出力: " + ToWide(r.output));
            }
            QueueDone(token, false, true);
            return;
        }
        QueueProgress(token, static_cast<WORD>(i + 1), static_cast<WORD>(steps.size() + 1));
    }

    QueueLog(token, L"━━ 書き込み完了 ━━");
    QueueDone(token, true, true);
}

