#include "app.h"

#include <iterator>

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

    MoveWindow(hBtnSettings, width - margin - 86, margin + 8, 68, 30, TRUE);

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
}
