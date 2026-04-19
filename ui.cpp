#include "app.h"

#include <algorithm>
#include <vector>

namespace {
constexpr int kMargin = 18;
constexpr int kGap = 14;
constexpr int kHeaderH = 118;
constexpr int kCardH = 186;
constexpr int kPad = 18;
constexpr int kLogPadding = 14;
constexpr int kLogTitleH = 22;
constexpr int kMaxVisibleLinesReserve = 4;

bool g_LogClassRegistered = false;

void AddRoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rc, float radius) {
    const float d = radius * 2.0f;
    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void PaintLogBackground(HDC hdc, const RECT& rc) {
    if (g_BackgroundBitmap) {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        const int srcW = static_cast<int>(g_BackgroundBitmap->GetWidth());
        const int srcH = static_cast<int>(g_BackgroundBitmap->GetHeight());
        const int dstW = rc.right - rc.left;
        const int dstH = rc.bottom - rc.top;
        if (srcW > 0 && srcH > 0 && dstW > 0 && dstH > 0) {
            const double scaleW = static_cast<double>(dstW) / static_cast<double>(srcW);
            const double scaleH = static_cast<double>(dstH) / static_cast<double>(srcH);
            const double scale = (scaleW < scaleH) ? scaleW : scaleH;
            const int drawW = static_cast<int>(static_cast<double>(srcW) * scale);
            const int drawH = static_cast<int>(static_cast<double>(srcH) * scale);
            const int x = rc.left + (dstW - drawW) / 2;
            const int y = rc.top + (dstH - drawH) / 2;

            Gdiplus::ColorMatrix matrix{};
            matrix.m[0][0] = 1.0f;
            matrix.m[1][1] = 1.0f;
            matrix.m[2][2] = 1.0f;
            matrix.m[3][3] = 0.16f;
            matrix.m[4][4] = 1.0f;

            Gdiplus::ImageAttributes attrs;
            attrs.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
            g.DrawImage(g_BackgroundBitmap, Gdiplus::Rect(x, y, drawW, drawH), 0, 0, srcW, srcH, Gdiplus::UnitPixel, &attrs);
        }
    }
}

void UpdateScrollInfo(HWND hwnd, int visibleLines) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    const int total = static_cast<int>(g_LogLines.size());
    const int maxPos = total > visibleLines ? total - visibleLines : 0;
    if (g_LogScrollPos > maxPos) {
        g_LogScrollPos = maxPos;
    }
    if (g_LogScrollPos < 0) {
        g_LogScrollPos = 0;
    }

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = maxPos;
    si.nPage = static_cast<UINT>(visibleLines > 0 ? visibleLines : 1);
    si.nPos = g_LogScrollPos;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

int MeasureLineHeight(HDC hdc) {
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    return tm.tmHeight + tm.tmExternalLeading + 2;
}

int ComputeVisibleLines(HWND hwnd, HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int h = rc.bottom - rc.top - (kLogPadding * 2) - kLogTitleH - 10;
    const int lineH = MeasureLineHeight(hdc);
    if (h <= 0 || lineH <= 0) {
        return 1;
    }
    const int visible = h / lineH;
    return visible > 1 ? visible : 1;
}

void PaintLogView(HWND hwnd, HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    PaintLogBackground(hdc, rc);

    Gdiplus::GraphicsPath path;
    Gdiplus::RectF rr(static_cast<float>(rc.left + 1), static_cast<float>(rc.top + 1),
                      static_cast<float>(rc.right - rc.left - 2), static_cast<float>(rc.bottom - rc.top - 2));
    AddRoundRectPath(path, rr, 16.0f);

    Gdiplus::SolidBrush fill(Gdiplus::Color(218,
        GetRValue(C_PANEL2),
        GetGValue(C_PANEL2),
        GetBValue(C_PANEL2)));
    g.FillPath(&fill, &path);

    Gdiplus::Pen pen(Gdiplus::Color(130, GetRValue(C_LINE), GetGValue(C_LINE), GetBValue(C_LINE)), 1.0f);
    g.DrawPath(&pen, &path);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, C_TEXT);
    SelectObject(hdc, g_hFontBody);

    RECT titleRc{rc.left + kLogPadding, rc.top + 10, rc.right - kLogPadding, rc.top + 32};
    DrawTextW(hdc, L"実行ログ", -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT subRc{rc.left + kLogPadding + 74, rc.top + 10, rc.right - kLogPadding, rc.top + 32};
    SetTextColor(hdc, C_MUTED);
    DrawTextW(hdc, L"fastboot の出力と進行状況を表示します。", -1, &subRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT bodyRc{rc.left + kLogPadding, rc.top + kLogPadding + 26, rc.right - kLogPadding, rc.bottom - kLogPadding};
    SelectObject(hdc, g_hFontMono);
    const int lineH = MeasureLineHeight(hdc);
    const int visibleLines = ComputeVisibleLines(hwnd, hdc);

    std::vector<std::wstring> lines;
    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        lines.assign(g_LogLines.begin(), g_LogLines.end());
    }

    const int total = static_cast<int>(lines.size());
    const int maxPos = total > visibleLines ? total - visibleLines : 0;
    if (g_LogScrollPos > maxPos) {
        g_LogScrollPos = maxPos;
    }
    if (g_LogScrollPos < 0) {
        g_LogScrollPos = 0;
    }
    UpdateScrollInfo(hwnd, visibleLines);

    const int start = (g_LogScrollPos >= total) ? maxPos : g_LogScrollPos;
    const int end = start + visibleLines;
    int y = bodyRc.top;
    SetTextColor(hdc, C_TEXT);
    SelectObject(hdc, g_hFontMono);

    for (int i = start; i < total && i < end; ++i) {
        RECT lineRc{bodyRc.left, y, bodyRc.right, y + lineH};
        const std::wstring& line = lines[static_cast<size_t>(i)];
        if (!line.empty()) {
            DrawTextW(hdc, line.c_str(), static_cast<int>(line.size()), &lineRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        y += lineH;
    }
}

void ScrollLog(HWND hwnd, int deltaLines) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    HDC hdc = GetDC(hwnd);
    int visible = 1;
    if (hdc) {
        visible = ComputeVisibleLines(hwnd, hdc);
        ReleaseDC(hwnd, hdc);
    }

    std::lock_guard<std::mutex> lock(g_LogMutex);
    const int total = static_cast<int>(g_LogLines.size());
    const int maxPos = total > visible ? total - visible : 0;
    g_LogScrollPos += deltaLines;
    if (g_LogScrollPos < 0) {
        g_LogScrollPos = 0;
    }
    if (g_LogScrollPos > maxPos) {
        g_LogScrollPos = maxPos;
    }
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = maxPos;
    si.nPage = static_cast<UINT>(visible > 0 ? visible : 1);
    si.nPos = g_LogScrollPos;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    InvalidateRect(hwnd, nullptr, TRUE);
}

LRESULT CALLBACK LogWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_SIZE: {
        HDC hdc = GetDC(hwnd);
        if (hdc) {
            const int visible = ComputeVisibleLines(hwnd, hdc);
            ReleaseDC(hwnd, hdc);
            UpdateScrollInfo(hwnd, visible);
        }
        return 0;
    }

    case WM_VSCROLL: {
        int code = LOWORD(wp);
        int delta = 0;
        switch (code) {
        case SB_LINEUP: delta = -1; break;
        case SB_LINEDOWN: delta = 1; break;
        case SB_PAGEUP: delta = -4; break;
        case SB_PAGEDOWN: delta = 4; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            g_LogScrollPos = si.nTrackPos;
            ScrollLog(hwnd, 0);
            return 0;
        }
        case SB_TOP:
            g_LogScrollPos = 0;
            ScrollLog(hwnd, 0);
            return 0;
        case SB_BOTTOM:
            g_LogScrollPos = INT_MAX;
            ScrollLog(hwnd, 0);
            return 0;
        default:
            return 0;
        }
        ScrollLog(hwnd, delta);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        const int z = GET_WHEEL_DELTA_WPARAM(wp);
        if (z > 0) {
            ScrollLog(hwnd, -3);
        } else if (z < 0) {
            ScrollLog(hwnd, 3);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintLogView(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace

void RegisterLogViewClass() {
    if (g_LogClassRegistered) {
        return;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = LogWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"A05BDLogView";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
    g_LogClassRegistered = true;
}

void LayoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;

    const int leftW = (width - kMargin * 2 - kGap) / 2;
    const int rightW = width - kMargin * 2 - kGap - leftW;
    const int headerBottom = kMargin + kHeaderH;
    const int cardBottom = headerBottom + kCardH;

    g_layout.header = {kMargin, kMargin, width - kMargin, headerBottom - 8};
    g_layout.leftCard = {kMargin, headerBottom, kMargin + leftW, cardBottom};
    g_layout.rightCard = {kMargin + leftW + kGap, headerBottom, kMargin + leftW + kGap + rightW, cardBottom};
    g_layout.logCard = {kMargin, cardBottom + kGap, width - kMargin, height - kMargin};

    const int leftPad = 18;
    const int rightPad = 18;
    const int optionW = 92;
    const int optionH = 30;

    if (hLblTitle) {
        MoveWindow(hLblTitle, g_layout.header.left + 2, g_layout.header.top + 2, 320, 34, TRUE);
    }
    if (hLblSub) {
        MoveWindow(hLblSub, g_layout.header.left + 2, g_layout.header.top + 40, 240, 22, TRUE);
    }
    if (hBtnOption) {
        MoveWindow(hBtnOption, width - kMargin - optionW, kMargin + 10, optionW, optionH, TRUE);
    }

    if (hLblStatus) {
        MoveWindow(hLblStatus, g_layout.rightCard.left + rightPad, g_layout.rightCard.top + 14, rightW - rightPad * 2, 24, TRUE);
    }
    if (hLblHint) {
        MoveWindow(hLblHint, g_layout.rightCard.left + rightPad, g_layout.rightCard.top + 42, rightW - rightPad * 2, 34, TRUE);
    }
    if (hBtnCheck) {
        MoveWindow(hBtnCheck, g_layout.rightCard.left + rightPad, g_layout.rightCard.top + 88, (rightW - rightPad * 2 - 12) / 2, 42, TRUE);
    }
    if (hBtnFlash) {
        MoveWindow(hBtnFlash, g_layout.rightCard.left + rightPad + (rightW - rightPad * 2 - 12) / 2 + 12, g_layout.rightCard.top + 88, (rightW - rightPad * 2 - 12) / 2, 42, TRUE);
    }
    if (hProgressBar) {
        MoveWindow(hProgressBar, g_layout.rightCard.left + rightPad, g_layout.rightCard.top + 138, rightW - rightPad * 2, 16, TRUE);
    }

    if (hLblDevice) {
        MoveWindow(hLblDevice, g_layout.leftCard.left + leftPad, g_layout.leftCard.top + 28, leftW - leftPad * 2, 22, TRUE);
    }
    if (hLblFastboot) {
        MoveWindow(hLblFastboot, g_layout.leftCard.left + leftPad, g_layout.leftCard.top + 60, leftW - leftPad * 2, 22, TRUE);
    }
    if (hLblRom) {
        MoveWindow(hLblRom, g_layout.leftCard.left + leftPad, g_layout.leftCard.top + 92, leftW - leftPad * 2, 22, TRUE);
    }
    if (hLblBgImage) {
        MoveWindow(hLblBgImage, g_layout.leftCard.left + leftPad, g_layout.leftCard.top + 124, leftW - leftPad * 2, 22, TRUE);
    }
    if (hLblSteps) {
        MoveWindow(hLblSteps, g_layout.leftCard.left + leftPad, g_layout.leftCard.top + 156, leftW - leftPad * 2, 22, TRUE);
    }

    if (hLog) {
        MoveWindow(hLog, g_layout.logCard.left + 10, g_layout.logCard.top + 10,
                   (g_layout.logCard.right - g_layout.logCard.left) - 20,
                   (g_layout.logCard.bottom - g_layout.logCard.top) - 20, TRUE);
    }
}

void DrawRoundCard(HDC hdc, const RECT& rc, COLORREF fill, COLORREF edge, int radius) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    Gdiplus::GraphicsPath path;
    Gdiplus::RectF rr(static_cast<float>(rc.left), static_cast<float>(rc.top),
                      static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top));
    AddRoundRectPath(path, rr, static_cast<float>(radius));

    Gdiplus::SolidBrush br(Gdiplus::Color(218, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    g.FillPath(&br, &path);

    Gdiplus::Pen pen(Gdiplus::Color(150, GetRValue(edge), GetGValue(edge), GetBValue(edge)), 1.0f);
    g.DrawPath(&pen, &path);
}

void DrawChip(HDC hdc, int x, int y, int w, int h, COLORREF fill, COLORREF edge, const wchar_t* text) {
    RECT rc{x, y, x + w, y + h};
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    Gdiplus::GraphicsPath path;
    AddRoundRectPath(path, Gdiplus::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top),
                                           static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top)), 12.0f);
    Gdiplus::SolidBrush br(Gdiplus::Color(225, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    Gdiplus::Pen pen(Gdiplus::Color(150, GetRValue(edge), GetGValue(edge), GetBValue(edge)), 1.0f);
    g.FillPath(&br, &path);
    g.DrawPath(&pen, &path);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, C_TEXT);
    SelectObject(hdc, g_hFontBody);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintMain(HDC hdc, const RECT& rc) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    Gdiplus::SolidBrush bg(Gdiplus::Color(255, GetRValue(C_BG), GetGValue(C_BG), GetBValue(C_BG)));
    g.FillRectangle(&bg, static_cast<float>(rc.left), static_cast<float>(rc.top),
                    static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top));

    if (g_BackgroundBitmap) {
        const int srcW = static_cast<int>(g_BackgroundBitmap->GetWidth());
        const int srcH = static_cast<int>(g_BackgroundBitmap->GetHeight());
        const int dstW = rc.right - rc.left;
        const int dstH = rc.bottom - rc.top;
        if (srcW > 0 && srcH > 0 && dstW > 0 && dstH > 0) {
            const double scaleW = static_cast<double>(dstW) / static_cast<double>(srcW);
            const double scaleH = static_cast<double>(dstH) / static_cast<double>(srcH);
            const double scale = (scaleW < scaleH) ? scaleW : scaleH;
            const int drawW = static_cast<int>(static_cast<double>(srcW) * scale);
            const int drawH = static_cast<int>(static_cast<double>(srcH) * scale);
            const int x = rc.left + (dstW - drawW) / 2;
            const int y = rc.top + (dstH - drawH) / 2;

            Gdiplus::ColorMatrix matrix{};
            matrix.m[0][0] = 1.0f;
            matrix.m[1][1] = 1.0f;
            matrix.m[2][2] = 1.0f;
            matrix.m[3][3] = 0.22f;
            matrix.m[4][4] = 1.0f;

            Gdiplus::ImageAttributes attrs;
            attrs.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
            g.DrawImage(g_BackgroundBitmap, Gdiplus::Rect(x, y, drawW, drawH), 0, 0, srcW, srcH, Gdiplus::UnitPixel, &attrs);
        }
    }

    Gdiplus::SolidBrush header(Gdiplus::Color(176, GetRValue(C_BG2), GetGValue(C_BG2), GetBValue(C_BG2)));
    g.FillRectangle(&header, static_cast<float>(rc.left), static_cast<float>(rc.top), static_cast<float>(rc.right - rc.left), 118.0f);

    Gdiplus::SolidBrush accent(Gdiplus::Color(255, GetRValue(C_ACCENT), GetGValue(C_ACCENT), GetBValue(C_ACCENT)));
    g.FillRectangle(&accent, static_cast<float>(rc.left + 18), 122.0f, static_cast<float>(rc.right - rc.left - 36), 2.0f);

    DrawRoundCard(hdc, g_layout.leftCard, C_PANEL, C_LINE);
    DrawRoundCard(hdc, g_layout.rightCard, C_PANEL, C_LINE);
    DrawRoundCard(hdc, g_layout.logCard, C_PANEL2, C_LINE);

    SetBkMode(hdc, TRANSPARENT);

    SelectObject(hdc, g_hFontTitle);
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

    DrawChip(hdc, rc.right - 320, 24, 140, 26, RGB(34, 44, 68), C_LINE, L"v1.2");
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
