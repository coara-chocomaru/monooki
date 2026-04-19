#include "app.h"

namespace {
struct Metrics {
    int margin;
    int gap;
    int headerH;
    int topCardH;
    int logCardH;
    int cardGap;
    int titlePad;
};

Metrics CalcMetrics(int width, int height) {
    Metrics m{};
    m.margin = 18;
    m.gap = 14;
    m.cardGap = 12;
    m.titlePad = 18;
    m.headerH = (height >= 760) ? 120 : 108;
    const int usable = height - m.margin * 2 - m.headerH - m.gap;
    const int minTop = 186;
    const int minLog = 152;
    int top = (usable > (minTop + minLog + m.gap)) ? (usable - minLog - m.gap) : minTop;
    if (top < minTop) top = minTop;
    int logH = usable - top - m.gap;
    if (logH < minLog) {
        logH = minLog;
        top = usable - logH - m.gap;
        if (top < minTop) top = minTop;
    }
    const int maxTop = (height >= 760) ? 232 : 214;
    if (top > maxTop) top = maxTop;
    if (top + m.gap + logH > usable) {
        logH = usable - top - m.gap;
    }
    if (logH < minLog) logH = minLog;
    m.topCardH = top;
    m.logCardH = logH;
    return m;
}

void MoveIfValid(HWND hwnd, int x, int y, int w, int h) {
    if (hwnd) {
        MoveWindow(hwnd, x, y, w, h, TRUE);
    }
}
} // namespace

void LayoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const Metrics m = CalcMetrics(width, height);

    const int contentTop = m.margin + m.headerH;
    const int contentBottom = height - m.margin;
    const int cardY = contentTop;
    const int logY = contentTop + m.topCardH + m.gap;
    const int innerWidth = width - m.margin * 2;
    const int leftW = (innerWidth - m.gap) / 2;
    const int rightW = innerWidth - m.gap - leftW;

    g_layout.header = {m.margin, m.margin, width - m.margin, m.margin + m.headerH - 10};
    g_layout.leftCard = {m.margin, cardY, m.margin + leftW, cardY + m.topCardH};
    g_layout.rightCard = {m.margin + leftW + m.gap, cardY, m.margin + leftW + m.gap + rightW, cardY + m.topCardH};
    g_layout.logCard = {m.margin, logY, width - m.margin, contentBottom};

    const int pad = m.titlePad;
    const int btnH = 40;
    const int btnY = g_layout.rightCard.top + 88;
    const int innerRight = rightW - pad * 2;
    const int midGap = 12;
    const int btnW = (innerRight - midGap) / 2;

    MoveIfValid(hLblStatus, g_layout.rightCard.left + pad, g_layout.rightCard.top + 14, innerRight, 24);
    MoveIfValid(hLblHint, g_layout.rightCard.left + pad, g_layout.rightCard.top + 42, innerRight, 34);
    MoveIfValid(hBtnCheck, g_layout.rightCard.left + pad, btnY, btnW, btnH);
    MoveIfValid(hBtnFlash, g_layout.rightCard.left + pad + btnW + midGap, btnY, btnW, btnH);
    MoveIfValid(hProgressBar, g_layout.rightCard.left + pad, g_layout.rightCard.top + 140, innerRight, 16);

    const int leftInner = leftW - pad * 2;
    MoveIfValid(hLblDevice, g_layout.leftCard.left + pad, g_layout.leftCard.top + 32, leftInner, 22);
    MoveIfValid(hLblFastboot, g_layout.leftCard.left + pad, g_layout.leftCard.top + 70, leftInner, 22);
    MoveIfValid(hLblRom, g_layout.leftCard.left + pad, g_layout.leftCard.top + 108, leftInner, 22);
    MoveIfValid(hLblBgImage, g_layout.leftCard.left + pad, g_layout.leftCard.top + 146, leftInner, 22);
    MoveIfValid(hLblSteps, g_layout.leftCard.left + pad, g_layout.leftCard.top + 176, leftInner, 20);

    MoveIfValid(hBtnOption, width - m.margin - 92, 22, 92, 28);
    MoveIfValid(hLog, g_layout.logCard.left + 14, g_layout.logCard.top + 42,
                (g_layout.logCard.right - g_layout.logCard.left) - 28,
                (g_layout.logCard.bottom - g_layout.logCard.top) - 56);
}
