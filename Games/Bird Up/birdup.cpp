#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

static int gW = 640, gH = 480;

static HBITMAP gBmp;
static HDC gMemDC;
static HFONT gFont;

static uint32_t gSeed = 1;
static uint32_t rnd_u32() { gSeed = gSeed * 1664525u + 1013904223u; return gSeed; }

enum { OB_COUNT = 4 };
struct Ob {
    float x;
    float gapY;
    int passed;
};

static Ob gObs[OB_COUNT];
static int gScore;
static int gAlive;

static float gBirdY;
static float gBirdV;

static DWORD gLastTick;
static int gSpaceDown;

static const int BIRD_X = 120;
static const int BIRD_R = 12;

static const int OB_W = 56;
static const int GAP_H = 150;
static const int OB_SPACING = 200;
static const float SPEED = 240.0f;
static const float GRAV = 1100.0f;
static const float JUMP_V = -380.0f;

static void reset_game()
{
    gScore = 0;
    gAlive = 1;
    gBirdY = gH * 0.5f;
    gBirdV = 0.0f;

    float startX = (float)gW + 120.0f;
    for (int i = 0; i < OB_COUNT; ++i) {
        gObs[i].x = startX + (float)(i * OB_SPACING);
        int margin = 60;
        int top = margin + GAP_H / 2;
        int bot = gH - margin - GAP_H / 2;
        int r = top + (int)(rnd_u32() % (uint32_t)(bot - top + 1));
        gObs[i].gapY = (float)r;
        gObs[i].passed = 0;
    }
}

static void resize_backbuffer(HDC hdc)
{
    if (!gMemDC) gMemDC = CreateCompatibleDC(hdc);
    if (gBmp) { DeleteObject(gBmp); gBmp = 0; }
    gBmp = CreateCompatibleBitmap(hdc, gW, gH);
    SelectObject(gMemDC, gBmp);
    if (!gFont) gFont = (HFONT)GetStockObject(ANSI_VAR_FONT);
    SelectObject(gMemDC, gFont);
    SetBkMode(gMemDC, TRANSPARENT);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static COLORREF lerp_rgb(COLORREF a, COLORREF b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int ar = GetRValue(a), ag = GetGValue(a), ab = GetBValue(a);
    int br = GetRValue(b), bg = GetGValue(b), bb = GetBValue(b);
    int rr = ar + (int)((br - ar) * t);
    int rg = ag + (int)((bg - ag) * t);
    int rb = ab + (int)((bb - ab) * t);
    return RGB(rr, rg, rb);
}

static void step_game(float dt)
{
    if (dt > 0.05f) dt = 0.05f;
    if (!gAlive) return;

    gBirdV += GRAV * dt;
    gBirdY += gBirdV * dt;

    if (gBirdY < (float)BIRD_R) { gBirdY = (float)BIRD_R; gBirdV = 0.0f; }
    if (gBirdY > (float)(gH - BIRD_R)) { gBirdY = (float)(gH - BIRD_R); gAlive = 0; }

    float maxX = 0.0f;
    for (int i = 0; i < OB_COUNT; ++i) if (gObs[i].x > maxX) maxX = gObs[i].x;

    for (int i = 0; i < OB_COUNT; ++i) {
        gObs[i].x -= SPEED * dt;

        if (!gObs[i].passed && gObs[i].x + (float)OB_W < (float)(BIRD_X - BIRD_R)) {
            gObs[i].passed = 1;
            ++gScore;
        }

        if (gObs[i].x < -(float)OB_W) {
            gObs[i].x = maxX + (float)OB_SPACING;
            maxX = gObs[i].x;
            int margin = 60;
            int top = margin + GAP_H / 2;
            int bot = gH - margin - GAP_H / 2;
            int r = top + (int)(rnd_u32() % (uint32_t)(bot - top + 1));
            gObs[i].gapY = (float)r;
            gObs[i].passed = 0;
        }

        int left = (int)gObs[i].x;
        int right = left + OB_W;
        int gapTop = (int)(gObs[i].gapY - GAP_H * 0.5f);
        int gapBot = (int)(gObs[i].gapY + GAP_H * 0.5f);
        gapTop = clampi(gapTop, 0, gH);
        gapBot = clampi(gapBot, 0, gH);

        int bx0 = BIRD_X - BIRD_R;
        int bx1 = BIRD_X + BIRD_R;
        int by0 = (int)gBirdY - BIRD_R;
        int by1 = (int)gBirdY + BIRD_R;
        if (bx1 > left && bx0 < right) {
            if (by0 < gapTop || by1 > gapBot) gAlive = 0;
        }
    }
}

static void draw_game()
{
    RECT r; r.left = 0; r.top = 0; r.right = gW; r.bottom = gH;

    // Background: subtle vertical gradient + faint stripes
    COLORREF skyTop = RGB(12, 16, 22);
    COLORREF skyBot = RGB(24, 30, 40);
    for (int y = 0; y < gH; y += 4) {
        float t = (gH > 1) ? (float)y / (float)(gH - 1) : 0.0f;
        COLORREF c = lerp_rgb(skyTop, skyBot, t);
        RECT rr = { 0, y, gW, (y + 4 < gH) ? y + 4 : gH };
        HBRUSH b = CreateSolidBrush(c);
        FillRect(gMemDC, &rr, b);
        DeleteObject(b);
    }
    for (int x = 0; x < gW; x += 40) {
        RECT sr = { x, 0, x + 1, gH };
        HBRUSH sb = CreateSolidBrush(RGB(28, 36, 48));
        FillRect(gMemDC, &sr, sb);
        DeleteObject(sb);
    }

    // Obstacles: body shading + outline + caps around the gap
    HPEN obOutline = CreatePen(PS_SOLID, 1, RGB(20, 60, 30));
    HBRUSH obMain = CreateSolidBrush(RGB(70, 200, 90));
    HBRUSH obHi = CreateSolidBrush(RGB(110, 230, 130));
    HBRUSH obLo = CreateSolidBrush(RGB(40, 145, 65));
    HBRUSH capMain = CreateSolidBrush(RGB(85, 220, 110));
    HBRUSH capHi = CreateSolidBrush(RGB(125, 240, 145));
    HBRUSH capLo = CreateSolidBrush(RGB(55, 170, 80));

    HGDIOBJ oldPen = SelectObject(gMemDC, obOutline);
    HGDIOBJ oldBrush = SelectObject(gMemDC, obMain);
    for (int i = 0; i < OB_COUNT; ++i) {
        int left = (int)gObs[i].x;
        int right = left + OB_W;
        int gapTop = (int)(gObs[i].gapY - GAP_H * 0.5f);
        int gapBot = (int)(gObs[i].gapY + GAP_H * 0.5f);
        gapTop = clampi(gapTop, 0, gH);
        gapBot = clampi(gapBot, 0, gH);

        // Top segment
        if (gapTop > 0) {
            SelectObject(gMemDC, obMain);
            Rectangle(gMemDC, left, 0, right, gapTop);

            int hiW = (OB_W >= 10) ? 8 : OB_W / 3;
            int loW = (OB_W >= 10) ? 8 : OB_W / 3;
            SelectObject(gMemDC, obHi);
            Rectangle(gMemDC, left + 1, 1, left + 1 + hiW, gapTop - 1);
            SelectObject(gMemDC, obLo);
            Rectangle(gMemDC, right - 1 - loW, 1, right - 1, gapTop - 1);

            // Cap at bottom of top segment
            int capH = 14;
            int capY0 = gapTop - capH;
            if (capY0 < 0) capY0 = 0;
            SelectObject(gMemDC, capMain);
            Rectangle(gMemDC, left - 3, capY0, right + 3, gapTop);
            SelectObject(gMemDC, capHi);
            Rectangle(gMemDC, left - 2, capY0 + 1, left + 6, gapTop - 1);
            SelectObject(gMemDC, capLo);
            Rectangle(gMemDC, right - 6, capY0 + 1, right + 2, gapTop - 1);
        }

        // Bottom segment
        if (gapBot < gH) {
            SelectObject(gMemDC, obMain);
            Rectangle(gMemDC, left, gapBot, right, gH);

            int hiW = (OB_W >= 10) ? 8 : OB_W / 3;
            int loW = (OB_W >= 10) ? 8 : OB_W / 3;
            SelectObject(gMemDC, obHi);
            Rectangle(gMemDC, left + 1, gapBot + 1, left + 1 + hiW, gH - 1);
            SelectObject(gMemDC, obLo);
            Rectangle(gMemDC, right - 1 - loW, gapBot + 1, right - 1, gH - 1);

            // Cap at top of bottom segment
            int capH = 14;
            int capY1 = gapBot + capH;
            if (capY1 > gH) capY1 = gH;
            SelectObject(gMemDC, capMain);
            Rectangle(gMemDC, left - 3, gapBot, right + 3, capY1);
            SelectObject(gMemDC, capHi);
            Rectangle(gMemDC, left - 2, gapBot + 1, left + 6, capY1 - 1);
            SelectObject(gMemDC, capLo);
            Rectangle(gMemDC, right - 6, gapBot + 1, right + 2, capY1 - 1);
        }
    }
    SelectObject(gMemDC, oldBrush);
    SelectObject(gMemDC, oldPen);
    DeleteObject(obOutline);
    DeleteObject(obMain);
    DeleteObject(obHi);
    DeleteObject(obLo);
    DeleteObject(capMain);
    DeleteObject(capHi);
    DeleteObject(capLo);

    // Bird: outline + highlight + eye + beak + wing + shadow
    int bx0 = BIRD_X - BIRD_R;
    int by0 = (int)gBirdY - BIRD_R;
    int bx1 = BIRD_X + BIRD_R;
    int by1 = (int)gBirdY + BIRD_R;

    // Drop shadow
    HPEN nullPen = (HPEN)GetStockObject(NULL_PEN);
    SelectObject(gMemDC, nullPen);
    HBRUSH sh = CreateSolidBrush(RGB(0, 0, 0));
    SelectObject(gMemDC, sh);
    Ellipse(gMemDC, bx0 + 4, by0 + 5, bx1 + 4, by1 + 5);
    DeleteObject(sh);

    // Body
    HPEN birdOutline = CreatePen(PS_SOLID, 2, RGB(170, 120, 20));
    HBRUSH birdBody = CreateSolidBrush(RGB(250, 220, 70));
    SelectObject(gMemDC, birdOutline);
    SelectObject(gMemDC, birdBody);
    Ellipse(gMemDC, bx0, by0, bx1, by1);
    DeleteObject(birdBody);

    // Highlight
    SelectObject(gMemDC, nullPen);
    HBRUSH birdHi = CreateSolidBrush(RGB(255, 245, 160));
    SelectObject(gMemDC, birdHi);
    Ellipse(gMemDC, bx0 + 3, by0 + 3, bx0 + BIRD_R, by0 + BIRD_R);
    DeleteObject(birdHi);

    // Wing
    HPEN wingPen = CreatePen(PS_SOLID, 1, RGB(160, 110, 25));
    HBRUSH wingBrush = CreateSolidBrush(RGB(235, 200, 55));
    SelectObject(gMemDC, wingPen);
    SelectObject(gMemDC, wingBrush);
    Ellipse(gMemDC, BIRD_X - 10, (int)gBirdY - 2, BIRD_X + 6, (int)gBirdY + 10);
    DeleteObject(wingBrush);
    DeleteObject(wingPen);

    // Eye
    SelectObject(gMemDC, nullPen);
    HBRUSH eyeWhite = CreateSolidBrush(RGB(250, 250, 250));
    SelectObject(gMemDC, eyeWhite);
    Ellipse(gMemDC, BIRD_X + 1, (int)gBirdY - 8, BIRD_X + 10, (int)gBirdY + 1);
    DeleteObject(eyeWhite);
    HBRUSH pupil = CreateSolidBrush(RGB(30, 30, 30));
    SelectObject(gMemDC, pupil);
    Ellipse(gMemDC, BIRD_X + 6, (int)gBirdY - 5, BIRD_X + 9, (int)gBirdY - 2);
    DeleteObject(pupil);

    // Beak
    POINT beak[3];
    beak[0].x = BIRD_X + BIRD_R - 1;
    beak[0].y = (int)gBirdY - 1;
    beak[1].x = BIRD_X + BIRD_R + 10;
    beak[1].y = (int)gBirdY + 2;
    beak[2].x = BIRD_X + BIRD_R - 1;
    beak[2].y = (int)gBirdY + 5;
    HPEN beakPen = CreatePen(PS_SOLID, 1, RGB(150, 80, 10));
    HBRUSH beakBrush = CreateSolidBrush(RGB(255, 150, 40));
    SelectObject(gMemDC, beakPen);
    SelectObject(gMemDC, beakBrush);
    Polygon(gMemDC, beak, 3);
    DeleteObject(beakBrush);
    DeleteObject(beakPen);

    DeleteObject(birdOutline);

    // UI text (with slight shadow)
    SetTextColor(gMemDC, RGB(0, 0, 0));
    char buf[64];
    wsprintfA(buf, "Score: %d", gScore);
    TextOutA(gMemDC, 13, 11, buf, lstrlenA(buf));
    SetTextColor(gMemDC, RGB(240, 240, 240));
    TextOutA(gMemDC, 12, 10, buf, lstrlenA(buf));

    if (!gAlive) {
        const char* msg = "GAME OVER - Press SPACE";
        int len = lstrlenA(msg);
        SIZE s; GetTextExtentPoint32A(gMemDC, msg, len, &s);
        int tx = (gW - s.cx) / 2;
        int ty = (gH - s.cy) / 2;
        SetTextColor(gMemDC, RGB(0, 0, 0));
        TextOutA(gMemDC, tx + 2, ty + 2, msg, len);
        SetTextColor(gMemDC, RGB(240, 240, 240));
        TextOutA(gMemDC, tx, ty, msg, len);
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        gSeed = (uint32_t)(GetTickCount() ^ (uintptr_t)h);
        gLastTick = GetTickCount();
        reset_game();
        return 0;
    case WM_SIZE: {
        gW = LOWORD(l);
        gH = HIWORD(l);
        if (gW < 1) gW = 1;
        if (gH < 1) gH = 1;
        HDC hdc = GetDC(h);
        resize_backbuffer(hdc);
        ReleaseDC(h, hdc);
    } return 0;
    case WM_KEYDOWN:
        if (w == VK_SPACE) {
            if (!gSpaceDown) {
                gSpaceDown = 1;
                if (gAlive) {
                    gBirdV = JUMP_V;
                } else {
                    reset_game();
                }
            }
        }
        return 0;
    case WM_KEYUP:
        if (w == VK_SPACE) gSpaceDown = 0;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int)
{
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hi;
    wc.lpszClassName = "BirdUp";
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    RegisterClassA(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, gW, gH };
    AdjustWindowRect(&r, style, FALSE);
    HWND h = CreateWindowA(wc.lpszClassName, "Bird Up", style,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        0, 0, hi, 0);
    ShowWindow(h, SW_SHOW);

    HDC hdc = GetDC(h);
    resize_backbuffer(hdc);
    ReleaseDC(h, hdc);

    MSG msg;
    for (;;) {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        DWORD now = GetTickCount();
        float dt = (now - gLastTick) * (1.0f / 1000.0f);
        gLastTick = now;
        step_game(dt);
        draw_game();

        HDC wdc = GetDC(h);
        BitBlt(wdc, 0, 0, gW, gH, gMemDC, 0, 0, SRCCOPY);
        ReleaseDC(h, wdc);

        Sleep(1);
    }

done:
    if (gBmp) DeleteObject(gBmp);
    if (gMemDC) DeleteDC(gMemDC);
    return 0;
}