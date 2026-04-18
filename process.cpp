#include "app.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
constexpr size_t kMaxQueuedLines = 384;
constexpr size_t kMaxLogChars = 48000;

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE h) : h_(h) {}
    ~Handle() { reset(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : h_(other.h_) { other.h_ = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            h_ = other.h_;
            other.h_ = nullptr;
        }
        return *this;
    }

    HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }

    HANDLE release() {
        HANDLE tmp = h_;
        h_ = nullptr;
        return tmp;
    }

    void reset(HANDLE h = nullptr) {
        if (h_) {
            CloseHandle(h_);
        }
        h_ = h;
    }

private:
    HANDLE h_{nullptr};
};

std::wstring NormalizeForDisplay(const std::wstring& text);
std::vector<std::wstring> SplitLogLines(const std::wstring& text);
void TrimLogWindow();
bool ContainsInsensitive(const std::string& haystack, const std::string& needle);

struct PartitionOption {
    bool flash{true};
    bool erase{true};
    bool img{true};
};

struct OptionConfig {
    bool present{false};
    PartitionOption defaults{};
    std::unordered_map<std::string, PartitionOption> partitions;
};

std::wstring ModuleDirW();
std::string WideToAnsi(const std::wstring& s);

std::string TrimAscii(std::string s) {
    auto is_ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    };
    size_t begin = 0;
    while (begin < s.size() && is_ws(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && is_ws(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string LowerAscii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool ParseBoolValue(const std::string& text, bool fallback) {
    const std::string v = LowerAscii(TrimAscii(text));
    if (v == "1" || v == "true" || v == "yes" || v == "on" || v == "enable" || v == "enabled") {
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off" || v == "disable" || v == "disabled") {
        return false;
    }
    return fallback;
}

std::string StripComment(std::string line) {
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && (ch == ';' || ch == '#')) {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string SectionKey(std::string name) {
    name = LowerAscii(TrimAscii(name));
    if (!name.empty() && ((name.front() == '[' && name.back() == ']') || (name.front() == '(' && name.back() == ')'))) {
        name = TrimAscii(name.substr(1, name.size() - 2));
    }
    return LowerAscii(name);
}

std::string OptionIniPathA() {
    return WideToAnsi(ModuleDirW() + L"\\option.ini");
}

PartitionOption GetPartitionOption(const OptionConfig& cfg, const std::string& key) {
    if (key.empty() || key == "default" || key == "*") {
        return cfg.defaults;
    }
    const auto it = cfg.partitions.find(key);
    if (it != cfg.partitions.end()) {
        return it->second;
    }
    return cfg.defaults;
}

OptionConfig LoadOptionConfig() {
    OptionConfig cfg{};
    const std::string path = OptionIniPathA();
    std::ifstream fin(path, std::ios::binary);
    if (!fin) {
        return cfg;
    }
    cfg.present = true;

    std::string line;
    std::string section = "default";
    while (std::getline(fin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line = TrimAscii(StripComment(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = SectionKey(line);
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = LowerAscii(TrimAscii(line.substr(0, eq)));
        std::string value = TrimAscii(line.substr(eq + 1));
        PartitionOption* dst = nullptr;
        if (section == "default" || section == "*") {
            dst = &cfg.defaults;
        } else {
            dst = &cfg.partitions[section];
        }
        if (key == "flash" || key == "flashcheck") {
            dst->flash = ParseBoolValue(value, dst->flash);
        } else if (key == "erase" || key == "erasecheck") {
            dst->erase = ParseBoolValue(value, dst->erase);
        } else if (key == "img" || key == "imgcheck" || key == "image" || key == "imagecheck") {
            dst->img = ParseBoolValue(value, dst->img);
        }
    }
    return cfg;
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

std::wstring NormalizeForDisplay(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                continue;
            }
            out.push_back(L'\n');
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::vector<std::wstring> SplitLogLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    if (text.empty()) {
        return lines;
    }

    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(L'\n', start);
        std::wstring line = (end == std::wstring::npos) ? text.substr(start) : text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

bool ContainsInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    if (haystack.empty() || haystack.size() < needle.size()) {
        return false;
    }

    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [&](char a, char b) { return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b)); });
    return it != haystack.end();
}

void TrimLogWindow() {
    if (!hLog) {
        return;
    }
    const int len = GetWindowTextLengthW(hLog);
    if (len <= static_cast<int>(kMaxLogChars)) {
        return;
    }
    const int removeCount = len - static_cast<int>(kMaxLogChars);
    SendMessageW(hLog, EM_SETSEL, 0, removeCount);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
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
    steps.push_back({FB("flash partition " + Img("pgpt.img")), L"0/34  PGPT 書き込み中…", "pgpt.img", "pgpt", StepKind::Flash});
    steps.push_back({FB("flash nvram " + Img("nvram.img")), L"1/34  NVRAM 書き込み中…", "nvram.img", "nvram", StepKind::Flash});
    steps.push_back({FB("flash nvcfg " + Img("nvcfg.img")), L"2/34  NVCFG 書き込み中…", "nvcfg.img", "nvcfg", StepKind::Flash});
    steps.push_back({FB("flash nvdata " + Img("nvdata.img")), L"3/34  NVDATA 書き込み中…", "nvdata.img", "nvdata", StepKind::Flash});
    steps.push_back({FB("flash persist " + Img("persist.img")), L"4/34  PERSIST 書き込み中…", "persist.img", "persist", StepKind::Flash});
    steps.push_back({FB("flash preloader " + Img("preloader.img")), L"5/34  preloader書き込み中…", "preloader.img", "preloader", StepKind::Flash});
    steps.push_back({FB("flash boot_para " + Img("boot_para.img")), L"6/34  BOOT_PARA 書き込み中…", "boot_para.img", "boot_para", StepKind::Flash});
    steps.push_back({FB("flash cam_vpu1 " + Img("cam_vpu1.img")), L"7/34  CAM_VPU1 書き込み中…", "cam_vpu1.img", "cam_vpu1", StepKind::Flash});
    steps.push_back({FB("flash cam_vpu2 " + Img("cam_vpu2.img")), L"8/34  CAM_VPU2 書き込み中…", "cam_vpu2.img", "cam_vpu2", StepKind::Flash});
    steps.push_back({FB("flash cam_vpu3 " + Img("cam_vpu3.img")), L"9/34  CAM_VPU3 書き込み中…", "cam_vpu3.img", "cam_vpu3", StepKind::Flash});
    steps.push_back({FB("erase protect1"), L"10/34  PROTECT1 消去中…", nullptr, "protect1", StepKind::Erase});
    steps.push_back({FB("erase protect2"), L"11/34  PROTECT2 消去中…", nullptr, "protect2", StepKind::Erase});
    steps.push_back({FB("erase nvram"), L"12/34  NVRAM 消去中…", nullptr, "nvram", StepKind::Erase});
    steps.push_back({FB("flash nvram " + Img("nvram.img")), L"13/34  NVRAM 書き込み中…", "nvram.img", "nvram", StepKind::Flash});
    steps.push_back({FB("flash lk " + Img("lk.img")), L"14/34  LK 書き込み中…", "lk.img", "lk", StepKind::Flash});
    steps.push_back({FB("flash lk2 " + Img("lk2.img")), L"15/34  LK2 書き込み中…", "lk2.img", "lk2", StepKind::Flash});
    steps.push_back({FB("flash boot " + Img("boot.img")), L"16/34  BOOT 書き込み中…", "boot.img", "boot", StepKind::Flash});
    steps.push_back({FB("flash recovery " + Img("recovery.img")), L"17/34  RECOVERY 書き込み中…", "recovery.img", "recovery", StepKind::Flash});
    steps.push_back({FB("flash logo " + Img("logo.img")), L"18/34  LOGO 書き込み中…", "logo.img", "logo", StepKind::Flash});
    steps.push_back({FB("flash dtbo " + Img("dtbo.img")), L"19/34  DTBO 書き込み中…", "dtbo.img", "dtbo", StepKind::Flash});
    steps.push_back({FB("erase expdb"), L"20/34  EXPDB 消去中…", nullptr, "expdb", StepKind::Erase});
    steps.push_back({FB("flash frp " + Img("frp.img")), L"21/34  FRP 書き込み中…", "frp.img", "frp", StepKind::Flash});
    steps.push_back({FB("erase para"), L"22/34  PARA 消去中…", nullptr, "para", StepKind::Erase});
    steps.push_back({FB("flash tee1 " + Img("tee.img")), L"23/34  TEE1 書き込み中…", "tee.img", "tee1", StepKind::Flash});
    steps.push_back({FB("flash tee2 " + Img("tee.img")), L"24/34  TEE2 書き込み中…", "tee.img", "tee2", StepKind::Flash});
    steps.push_back({FB("erase kb"), L"25/34  KB 消去中…", nullptr, "kb", StepKind::Erase});
    steps.push_back({FB("erase dkb"), L"26/34  DKB 消去中…", nullptr, "dkb", StepKind::Erase});
    steps.push_back({FB("erase metadata"), L"27/34  METADATA 消去中…", nullptr, "metadata", StepKind::Erase});
    steps.push_back({FB("flash vbmeta " + Img("vbmeta.img")), L"28/34  VBMETA 書き込み中…", "vbmeta.img", "vbmeta", StepKind::Flash});
    steps.push_back({FB("flash system " + Img("system.img")), L"29/34  SYSTEM 書き込み中…", "system.img", "system", StepKind::Flash});
    steps.push_back({FB("flash vendor " + Img("vendor.img")), L"30/34  VENDOR 書き込み中…", "vendor.img", "vendor", StepKind::Flash});
    steps.push_back({FB("flash factory " + Img("factory.img")), L"31/34  FACTORY 書き込み中…", "factory.img", "factory", StepKind::Flash});
    steps.push_back({FB("flash cache " + Img("cache.img")), L"32/34  CACHE 書き込み中…", "cache.img", "cache", StepKind::Flash});
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
        QueueLog(token, std::wstring(L"原因: ") + d.launchError);
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
        QueueLog(token, std::wstring(L"原因: ") + v.launchError);
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
        QueueLog(token, std::wstring(L"原因: ") + u.launchError);
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

    const auto cfg = LoadOptionConfig();
    if (cfg.present) {
        QueueLog(token, L"option.ini を確認しました。partition ごとの実行設定を反映します。");
    } else {
        QueueLog(token, L"option.ini が見つかりません。既定値で全チェックを行います。");
    }

    if (!FileExistsA(FASTBOOT_EXE())) {
        QueueLog(token, L"エラー: fastboot.exe が見つかりません。");
        QueueDone(token, false, true);
        return;
    }

    const auto steps = BuildFlashSteps();
    std::vector<FlashStep> active;
    active.reserve(steps.size());

    for (const auto& step : steps) {
        const auto opt = GetPartitionOption(cfg, step.partition ? step.partition : "");
        const bool execute = (step.kind == StepKind::Flash) ? opt.flash : opt.erase;
        if (!execute) {
            QueueLog(token, std::wstring(L"スキップ: ") + step.desc + L" (option.ini で false)");
            continue;
        }
        active.push_back(step);
    }

    for (const auto& step : active) {
        const auto opt = GetPartitionOption(cfg, step.partition ? step.partition : "");
        if (step.kind == StepKind::Flash && step.asset && opt.img) {
            if (!FileExistsA(AssetPath(step.asset))) {
                QueueLog(token, L"エラー: 画像ファイルが見つかりません。");
                QueueLog(token, std::wstring(L"不足: ") + ToWide(AssetPath(step.asset)));
                QueueDone(token, false, true);
                return;
            }
        }
    }

    QueueProgress(token, 0, static_cast<WORD>(active.size() + 1));

    for (size_t i = 0; i < active.size(); ++i) {
        const auto& step = active[i];
        QueueLog(token, step.desc);
        auto r = Exec(step.cmd);
        if (!r.launchError.empty()) {
            QueueLog(token, L"失敗  起動エラー");
            QueueLog(token, std::wstring(L"原因: ") + r.launchError);
            QueueDone(token, false, true);
            return;
        }
        if (r.exitCode != 0) {
            QueueLog(token, L"失敗  終了コード=" + std::to_wstring(r.exitCode));
            if (!r.output.empty()) {
                QueueLog(token, std::wstring(L"出力: ") + ToWide(r.output));
            }
            QueueDone(token, false, true);
            return;
        }
        QueueProgress(token, static_cast<WORD>(i + 1), static_cast<WORD>(active.size() + 1));
    }

    QueueLog(token, L"最終処理: reboot-recovery");
    auto end = Exec(FB("oem reboot-recovery"));
    if (!end.launchError.empty()) {
        QueueLog(token, L"失敗  起動エラー");
        QueueLog(token, std::wstring(L"原因: ") + end.launchError);
        QueueDone(token, false, true);
        return;
    }
    if (end.exitCode != 0) {
        QueueLog(token, L"失敗  終了コード=" + std::to_wstring(end.exitCode));
        if (!end.output.empty()) {
            QueueLog(token, std::wstring(L"出力: ") + ToWide(end.output));
        }
        QueueDone(token, false, true);
        return;
    }

    QueueProgress(token, static_cast<WORD>(active.size() + 1), static_cast<WORD>(active.size() + 1));
    QueueLog(token, L"==============================");
    QueueLog(token, L"すべての書き込みに成功しました。");
    QueueDone(token, true, true);
}
