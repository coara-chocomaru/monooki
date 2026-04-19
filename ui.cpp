#include "app.h"

void LayoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int margin = 18;
    const int gap = 14;
    const int headerH = 110;
    const int cardsH = 214;

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
    const int innerRight = rightW - pad * 2;
    const int halfGap = 12;

    MoveWindow(hLblStatus, g_layout.rightCard.left + pad, g_layout.rightCard.top + 14, innerRight, 24, TRUE);
    MoveWindow(hLblHint, g_layout.rightCard.left + pad, g_layout.rightCard.top + 42, innerRight, 30, TRUE);
    MoveWindow(hBtnCheck, g_layout.rightCard.left + pad, g_layout.rightCard.top + 86, (innerRight - halfGap) / 2, 42, TRUE);
    MoveWindow(hBtnFlash, g_layout.rightCard.left + pad + (innerRight - halfGap) / 2 + halfGap, g_layout.rightCard.top + 86, (innerRight - halfGap) / 2, 42, TRUE);
    MoveWindow(hProgressBar, g_layout.rightCard.left + pad, g_layout.rightCard.top + 136, innerRight, 16, TRUE);

    MoveWindow(hLblDevice, g_layout.leftCard.left + pad, g_layout.leftCard.top + 32, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblFastboot, g_layout.leftCard.left + pad, g_layout.leftCard.top + 72, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblRom, g_layout.leftCard.left + pad, g_layout.leftCard.top + 112, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblBgImage, g_layout.leftCard.left + pad, g_layout.leftCard.top + 152, leftW - pad * 2, 22, TRUE);
    MoveWindow(hLblSteps, g_layout.leftCard.left + pad, g_layout.leftCard.top + 178, leftW - pad * 2, 18, TRUE);

    MoveWindow(hBtnOption, width - margin - 84, 22, 84, 28, TRUE);
    MoveWindow(hLog, g_layout.logCard.left + 14, g_layout.logCard.top + 42, (g_layout.logCard.right - g_layout.logCard.left) - 28, (g_layout.logCard.bottom - g_layout.logCard.top) - 56, TRUE);
}
