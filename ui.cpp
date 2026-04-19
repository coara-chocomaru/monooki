#include "app.h"

void LayoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int margin = 18;
    const int gap = 14;
    const int headerH = 110;
    const int cardsH = 164;
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int cardTop = margin + headerH;
    const int leftW = (width - margin * 2 - gap) / 2;
    const int rightW = width - margin * 2 - gap - leftW;

    g_layout.header = {margin, margin, width - margin, margin + headerH - 8};
    g_layout.leftCard = {margin, cardTop, margin + leftW, cardTop + cardsH};
    g_layout.rightCard = {margin + leftW + gap, cardTop, margin + leftW + gap + rightW, cardTop + cardsH};
    g_layout.logCard = {margin, cardTop + cardsH + gap, width - margin, height - margin};

    const int pad = 18;

    MoveWindow(hLblStatus, g_layout.rightCard.left + pad, g_layout.rightCard.top + 14, rightW - pad * 2, 24, TRUE);
    MoveWindow(hLblHint, g_layout.rightCard.left + pad, g_layout.rightCard.top + 42, rightW - pad * 2, 30, TRUE);
    MoveWindow(hBtnCheck, g_layout.rightCard.left + pad, g_layout.rightCard.top + 86, (rightW - pad * 2 - 12) / 2, 42, TRUE);
    MoveWindow(hBtnFlash, g_layout.rightCard.left + pad + (rightW - pad * 2 - 12) / 2 + 12, g_layout.rightCard.top + 86, (rightW - pad * 2 - 12) / 2, 42, TRUE);
    MoveWindow(hProgressBar, g_layout.rightCard.left + pad, g_layout.rightCard.top + 136, rightW - pad * 2, 16, TRUE);

    MoveWindow(hLblDevice, g_layout.leftCard.left + pad, g_layout.leftCard.top + 38, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblFastboot, g_layout.leftCard.left + pad, g_layout.leftCard.top + 82, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblRom, g_layout.leftCard.left + pad, g_layout.leftCard.top + 126, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblSteps, g_layout.leftCard.left + pad, g_layout.leftCard.top + 146, leftW - pad * 2, 18, TRUE);

    MoveWindow(hLog, g_layout.logCard.left + 14, g_layout.logCard.top + 42, (g_layout.logCard.right - g_layout.logCard.left) - 28, (g_layout.logCard.bottom - g_layout.logCard.top) - 56, TRUE);

    MoveWindow(hBtnSettings, rc.right - margin - 80, margin + 24, 80, 32, TRUE);
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

    if (!g_Config.bgImage.empty()) {
        Gdiplus::Image img(g_Config.bgImage.c_str());
        if (img.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(hdc);
            Gdiplus::ColorMatrix cm = {};
            cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = cm.m[4][4] = 1.0f;
            cm.m[3][3] = 0.2f; 
            Gdiplus::ImageAttributes ia;
            ia.SetColorMatrix(&cm);
            g.DrawImage(&img, Gdiplus::Rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top),
                        0, 0, img.GetWidth(), img.GetHeight(), Gdiplus::UnitPixel, &ia);
        }
    }

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

    DrawChip(hdc, rc.right - 260, 26, 70, 26, RGB(34, 44, 68), C_LINE, L"v1.3");

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
