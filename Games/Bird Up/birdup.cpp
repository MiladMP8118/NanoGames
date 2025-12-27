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
    HBRUSH bg = CreateSolidBrush(RGB(18, 22, 28));
    FillRect(gMemDC, &r, bg);
    DeleteObject(bg);

    HPEN pen = (HPEN)GetStockObject(NULL_PEN);
    SelectObject(gMemDC, pen);

    HBRUSH ob = CreateSolidBrush(RGB(70, 200, 90));
    SelectObject(gMemDC, ob);
    for (int i = 0; i < OB_COUNT; ++i) {
        int left = (int)gObs[i].x;
        int right = left + OB_W;
        int gapTop = (int)(gObs[i].gapY - GAP_H * 0.5f);
        int gapBot = (int)(gObs[i].gapY + GAP_H * 0.5f);
        Rectangle(gMemDC, left, 0, right, gapTop);
        Rectangle(gMemDC, left, gapBot, right, gH);
    }
    DeleteObject(ob);

    HBRUSH bird = CreateSolidBrush(RGB(250, 220, 70));
    SelectObject(gMemDC, bird);
    int bx0 = BIRD_X - BIRD_R;
    int by0 = (int)gBirdY - BIRD_R;
    int bx1 = BIRD_X + BIRD_R;
    int by1 = (int)gBirdY + BIRD_R;
    Ellipse(gMemDC, bx0, by0, bx1, by1);
    DeleteObject(bird);

    SetTextColor(gMemDC, RGB(240, 240, 240));
    char buf[64];
    wsprintfA(buf, "Score: %d", gScore);
    TextOutA(gMemDC, 12, 10, buf, lstrlenA(buf));

    if (!gAlive) {
        const char* msg = "GAME OVER - Press SPACE";
        int len = lstrlenA(msg);
        SIZE s; GetTextExtentPoint32A(gMemDC, msg, len, &s);
        TextOutA(gMemDC, (gW - s.cx) / 2, (gH - s.cy) / 2, msg, len);
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
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
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