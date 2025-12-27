#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>


// ======================================================
// Backbuffer (DIBSection + Memory DC)  -> no flicker text
// ======================================================
static bool g_running = true;

static BITMAPINFO g_bmi{};
static void* g_pixels = nullptr;
static int g_w = 0, g_h = 0;

static HDC g_memDC = NULL;
static HBITMAP g_dib = NULL;
static HBITMAP g_oldBmp = NULL;
static HFONT g_font = NULL;

static inline uint32_t RGBX(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
}

static void DestroyBackbuffer() {
    if (g_memDC) {
        if (g_dib) {
            SelectObject(g_memDC, g_oldBmp);
            DeleteObject(g_dib);
            g_dib = NULL;
            g_pixels = nullptr;
        }
        DeleteDC(g_memDC);
        g_memDC = NULL;
    }
    if (g_font) {
        DeleteObject(g_font);
        g_font = NULL;
    }
}

static void ResizeBackbuffer(HWND hwnd, int w, int h) {
    g_w = (w > 0) ? w : 1;
    g_h = (h > 0) ? h : 1;

    if (!g_memDC) {
        HDC hdc = GetDC(hwnd);
        g_memDC = CreateCompatibleDC(hdc);
        ReleaseDC(hwnd, hdc);
    }

    if (g_dib) {
        SelectObject(g_memDC, g_oldBmp);
        DeleteObject(g_dib);
        g_dib = NULL;
        g_pixels = nullptr;
    }

    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = g_w;
    g_bmi.bmiHeader.biHeight = -g_h; // top-down
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    g_dib = CreateDIBSection(g_memDC, &g_bmi, DIB_RGB_COLORS, &g_pixels, NULL, 0);
    g_oldBmp = (HBITMAP)SelectObject(g_memDC, g_dib);

    if (!g_font) {
        g_font = CreateFontA(
            18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            "Segoe UI"
        );
    }
}

static void Clear(uint32_t color) {
    uint32_t* p = (uint32_t*)g_pixels;
    for (int y = 0; y < g_h; y++) {
        for (int x = 0; x < g_w; x++) {
            p[y * g_w + x] = color;
        }
    }
}

static void FillRectI(int x0, int y0, int x1, int y1, uint32_t color) {
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > g_w) x1 = g_w; if (y1 > g_h) y1 = g_h;
    if (x1 <= x0 || y1 <= y0) return;

    uint32_t* p = (uint32_t*)g_pixels;
    for (int y = y0; y < y1; y++) {
        uint32_t* row = p + y * g_w + x0;
        for (int x = x0; x < x1; x++) *row++ = color;
    }
}

static void DrawCenterLine(uint32_t color) {
    int x = g_w / 2;
    for (int y = 0; y < g_h; y += 18) {
        FillRectI(x - 2, y, x + 2, y + 10, color);
    }
}

static void DrawTextBB(int x, int y, const char* text, COLORREF color = RGB(240, 240, 240)) {
    SetBkMode(g_memDC, TRANSPARENT);
    SetTextColor(g_memDC, color);
    HFONT old = (HFONT)SelectObject(g_memDC, g_font);
    TextOutA(g_memDC, x, y, text, (int)lstrlenA(text));
    SelectObject(g_memDC, old);
}

// ======================================================
// Input (simple key state + edge detection)
// ======================================================
static bool g_keyDown[256]{};
static bool g_keyPressed[256]{};

static void BeginInputFrame() {
    for (int i = 0; i < 256; i++) g_keyPressed[i] = false;
}
static void OnKeyDown(uint8_t vk) {
    if (!g_keyDown[vk]) g_keyPressed[vk] = true;
    g_keyDown[vk] = true;
}
static void OnKeyUp(uint8_t vk) { g_keyDown[vk] = false; }

// ======================================================
// Pong game state
// ======================================================
struct Paddle {
    float x, y;      // center
    float w, h;
    float speed;
};

struct Ball {
    float x, y;
    float r;
    float vx, vy;
    bool inPlay;
};

enum AppState {
    STATE_MENU = 0,
    STATE_PLAYING = 1,
};

static Paddle g_left{}, g_right{};
static Ball g_ball{};
static int g_scoreL = 0, g_scoreR = 0;

static AppState g_state = STATE_MENU;
// 0 = 2 Players, 1 = Player vs Computer
static int g_menuSelection = 0;

// AI mode
static bool g_aiMode = false;
static float g_aiTargetY = 0.0f;
static int g_aiMoveDelayFrames = 0;
static int g_aiMoveFrameCounter = 0;
static int g_aiCheckFrameCounter = 0;
static int g_aiHitCount = 0;
static int g_aiMaxMoveDelay = 10;
static float g_aiCmdVelY = 0.0f;
static float g_aiVelY = 0.0f;

static float Clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void EnterStartMenu() {
    g_state = STATE_MENU;
    g_menuSelection = 0;
    g_ball.inPlay = false;
}

static void ResetRound(bool serveToRight) {
    g_ball.x = g_w * 0.5f;
    g_ball.y = g_h * 0.5f;
    g_ball.vx = serveToRight ? 320.0f : -320.0f;
    g_ball.vy = (serveToRight ? 1.0f : -1.0f) * 120.0f;
    g_ball.inPlay = false;
    
    // Reset AI movement delay when round starts
    if (g_aiMode) {
        g_aiMoveFrameCounter = 0;
        g_aiCheckFrameCounter = 0;
        g_aiCmdVelY = 0.0f;
        g_aiVelY = 0.0f;
        // Determine movement delay based on score difference
        int scoreDiff = g_scoreL - g_scoreR;
        if (scoreDiff >= 2) {
            g_aiMaxMoveDelay = 2;
        } else {
            g_aiMaxMoveDelay = (rand() % 2 == 0) ? 6 : 12; // Either 6 or 12 frames
        }
        g_aiMoveDelayFrames = g_aiMaxMoveDelay;
        g_aiTargetY = g_right.y;
    }
}

static void ResetGame() {
    g_scoreL = g_scoreR = 0;

    g_left.w = 14;  g_left.h = 110; g_left.speed = 520.0f;
    g_right.w = 14; g_right.h = 110; g_right.speed = 520.0f;

    g_left.x = 40.0f;
    g_right.x = (float)g_w - 40.0f;
    g_left.y = g_right.y = g_h * 0.5f;

    g_ball.r = 8.0f;
    
    // Reset AI state
    g_aiHitCount = 0;
    g_aiMoveFrameCounter = 0;
    g_aiCheckFrameCounter = 0;
    g_aiMoveDelayFrames = 10;
    g_aiMaxMoveDelay = 10;
    g_aiCmdVelY = 0.0f;
    g_aiVelY = 0.0f;
    
    ResetRound(true);
}

static bool CircleAABB(float cx, float cy, float r, float rx0, float ry0, float rx1, float ry1) {
    float closestX = Clamp(cx, rx0, rx1);
    float closestY = Clamp(cy, ry0, ry1);
    float dx = cx - closestX;
    float dy = cy - closestY;
    return (dx*dx + dy*dy) <= (r*r);
}

static void BounceFromPaddle(const Paddle& p, bool isLeft) {
    float rel = (g_ball.y - p.y) / (p.h * 0.5f);
    rel = Clamp(rel, -1.0f, 1.0f);

    float baseSpeed = 240.0f;
    float extra = 70.0f * fabsf(rel);

    float dir = isLeft ? 1.0f : -1.0f;
    g_ball.vx = dir * (baseSpeed + extra);
    g_ball.vy = rel * 320.0f;

    if (isLeft) g_ball.x = p.x + p.w * 0.5f + g_ball.r + 1.0f;
    else        g_ball.x = p.x - p.w * 0.5f - g_ball.r - 1.0f;
    
    // Track AI hits for perfect response feature
    if (g_aiMode && !isLeft) {
        g_aiHitCount++;
        g_aiMoveFrameCounter = 0;
        g_aiCmdVelY = 0.0f;
        g_aiVelY = 0.0f;
        
        // Every 7th hit gets perfect response (no movement delay)
        if (g_aiHitCount % 7 == 0) {
            g_aiMoveDelayFrames = 0;
        } else {
            // Adaptive movement delay based on score
            int scoreDiff = g_scoreL - g_scoreR;
            if (scoreDiff >= 2) {
                g_aiMaxMoveDelay = 2;
            } else {
                g_aiMaxMoveDelay = (rand() % 2 == 0) ? 6 : 12; // Either 6 or 12 frames
            }
            g_aiMoveDelayFrames = g_aiMaxMoveDelay;
        }
        g_aiTargetY = g_right.y;
    }
}

static void UpdateGame(float dt) {
    if (g_keyPressed[VK_ESCAPE]) g_running = false;
    if (g_keyPressed['R']) ResetGame();

    // Start menu: choose mode before playing
    if (g_state == STATE_MENU) {
        if (g_keyPressed[VK_UP] || g_keyPressed['W']) g_menuSelection--;
        if (g_keyPressed[VK_DOWN] || g_keyPressed['S']) g_menuSelection++;
        g_menuSelection = (int)Clamp((float)g_menuSelection, 0.0f, 1.0f);

        if (g_keyPressed['1']) g_menuSelection = 0;
        if (g_keyPressed['2']) g_menuSelection = 1;

        if (g_keyPressed[VK_RETURN] || g_keyPressed[VK_SPACE] || g_keyPressed['1'] || g_keyPressed['2']) {
            g_aiMode = (g_menuSelection == 1);
            ResetGame();
            g_state = STATE_PLAYING;
        }
        return;
    }

    // Left paddle (always human controlled)
    float dyL = 0.0f;
    if (g_keyDown['W']) dyL -= g_left.speed * dt;
    if (g_keyDown['S']) dyL += g_left.speed * dt;
    g_left.y = Clamp(g_left.y + dyL, g_left.h * 0.5f, g_h - g_left.h * 0.5f);

    // Right paddle (human or AI)
    float dyR = 0.0f;
    if (g_aiMode) {
        // AI updates its perceived ball height only every 24 frames (reaction sampling)
        if (g_ball.inPlay && g_ball.vx > 0) { // Ball moving towards AI
            g_aiCheckFrameCounter++;
            if (g_aiCheckFrameCounter >= 24) {
                g_aiTargetY = g_ball.y;
                g_aiCheckFrameCounter = 0;
            }
        }

        // Smooth movement: update a commanded velocity every N frames, then ease actual velocity toward it.
        g_aiMoveFrameCounter++;
        const int delay = (g_aiMoveDelayFrames < 0) ? 0 : g_aiMoveDelayFrames;
        if (delay == 0 || g_aiMoveFrameCounter >= delay) {
            float diff = g_aiTargetY - g_right.y;
            const float deadZonePx = 2.0f;
            const float kp = 8.0f; // px -> px/sec
            if (fabsf(diff) <= deadZonePx) {
                g_aiCmdVelY = 0.0f;
            } else {
                g_aiCmdVelY = Clamp(diff * kp, -g_right.speed, g_right.speed);
            }
            g_aiMoveFrameCounter = 0;
        }

        // Ease actual velocity toward command (prevents jitter when diff sign flips)
        const float accel = 3200.0f; // px/sec^2
        float dv = g_aiCmdVelY - g_aiVelY;
        float maxDv = accel * dt;
        g_aiVelY += Clamp(dv, -maxDv, +maxDv);

        dyR = g_aiVelY * dt;

        // Prevent overshoot: if we're about to cross the target, snap to it and zero velocity.
        float diffNow = g_aiTargetY - g_right.y;
        if (fabsf(diffNow) <= fabsf(dyR)) {
            dyR = diffNow;
            g_aiVelY = 0.0f;
            g_aiCmdVelY = 0.0f;
        }
    } else {
        // Human control
        if (g_keyDown[VK_UP]) dyR -= g_right.speed * dt;
        if (g_keyDown[VK_DOWN]) dyR += g_right.speed * dt;
    }
    g_right.y = Clamp(g_right.y + dyR, g_right.h * 0.5f, g_h - g_right.h * 0.5f);

    if (!g_ball.inPlay && g_keyPressed[VK_SPACE]) g_ball.inPlay = true;

    if (g_ball.inPlay) {
        g_ball.x += g_ball.vx * dt;
        g_ball.y += g_ball.vy * dt;

        if (g_ball.y - g_ball.r < 0) {
            g_ball.y = g_ball.r;
            g_ball.vy = -g_ball.vy;
        }
        if (g_ball.y + g_ball.r > g_h) {
            g_ball.y = g_h - g_ball.r;
            g_ball.vy = -g_ball.vy;
        }

        float lx0 = g_left.x - g_left.w * 0.5f;
        float ly0 = g_left.y - g_left.h * 0.5f;
        float lx1 = g_left.x + g_left.w * 0.5f;
        float ly1 = g_left.y + g_left.h * 0.5f;

        float rx0 = g_right.x - g_right.w * 0.5f;
        float ry0 = g_right.y - g_right.h * 0.5f;
        float rx1 = g_right.x + g_right.w * 0.5f;
        float ry1 = g_right.y + g_right.h * 0.5f;

        if (g_ball.vx < 0 && CircleAABB(g_ball.x, g_ball.y, g_ball.r, lx0, ly0, lx1, ly1)) {
            BounceFromPaddle(g_left, true);
        } else if (g_ball.vx > 0 && CircleAABB(g_ball.x, g_ball.y, g_ball.r, rx0, ry0, rx1, ry1)) {
            BounceFromPaddle(g_right, false);
        }

        if (g_ball.x + g_ball.r < 0) {
            g_scoreR++;
            ResetRound(false);
        } else if (g_ball.x - g_ball.r > g_w) {
            g_scoreL++;
            ResetRound(true);
        }
    }
}

static void RenderGame(HWND hwnd) {
    (void)hwnd;
}

// ======================================================
// Window Proc
// ======================================================
static HWND g_hwnd = NULL;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        g_running = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        ResizeBackbuffer(hwnd, w, h);
        ResetGame();
        return 0;
    }
    case WM_KEYDOWN:
        OnKeyDown((uint8_t)wParam);
        return 0;
    case WM_KEYUP:
        OnKeyUp((uint8_t)wParam);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// ======================================================
// WinMain + Loop
// ======================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    const char* CLASS_NAME = "PongWindow";

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(
        0, CLASS_NAME, "PONG (GDI, single-file, no flicker)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInst, NULL
    );

    ShowWindow(g_hwnd, nCmdShow);

    RECT r; GetClientRect(g_hwnd, &r);
    ResizeBackbuffer(g_hwnd, r.right - r.left, r.bottom - r.top);
    ResetGame();

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    LARGE_INTEGER last;
    QueryPerformanceCounter(&last);

    const double target_dt = 1.0 / 60.0;

    while (g_running) {
        BeginInputFrame();

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
        last = now;
        if (dt > 0.05) dt = 0.05;

        UpdateGame((float)dt);

        // Render everything to backbuffer
        Clear(RGBX(30, 30, 40));
        DrawCenterLine(RGBX(80, 80, 95));

        if (g_state == STATE_MENU) {
            const int cx = g_w / 2;
            const int top = g_h / 2 - 90;
            DrawTextBB(cx - 30, top, "PONG");

            const char* opt0 = "1) 2 Players";
            const char* opt1 = "2) Player vs Computer";
            COLORREF normal = RGB(240, 240, 240);
            COLORREF hi = RGB(255, 235, 150);

            DrawTextBB(cx - 120, top + 45,  opt0, (g_menuSelection == 0) ? hi : normal);
            DrawTextBB(cx - 120, top + 70,  opt1, (g_menuSelection == 1) ? hi : normal);
            DrawTextBB(cx - 120, top + 110, "Use Up/Down then Enter (or press 1/2)");
            DrawTextBB(cx - 120, top + 130, "ESC = Quit");
        } else {
            uint32_t paddleC = RGBX(15, 232, 73);
            FillRectI((int)(g_left.x - g_left.w * 0.5f),  (int)(g_left.y - g_left.h * 0.5f),
                      (int)(g_left.x + g_left.w * 0.5f),  (int)(g_left.y + g_left.h * 0.5f), paddleC);

            FillRectI((int)(g_right.x - g_right.w * 0.5f), (int)(g_right.y - g_right.h * 0.5f),
                      (int)(g_right.x + g_right.w * 0.5f), (int)(g_right.y + g_right.h * 0.5f), paddleC);

            uint32_t ballC = RGBX(252, 186, 4);
            FillRectI((int)(g_ball.x - g_ball.r), (int)(g_ball.y - g_ball.r),
                      (int)(g_ball.x + g_ball.r), (int)(g_ball.y + g_ball.r), ballC);

            char hud[180];
            const char* mode = g_aiMode ? "vs Computer" : "2 Players";
            wsprintfA(hud, "W/S (Left)   Up/Down (Right)   Space=Serve   R=Reset   Mode: %s   Score: %d - %d", mode, g_scoreL, g_scoreR);
            DrawTextBB(12, 10, hud);

            if (!g_ball.inPlay) {
                DrawTextBB(g_w / 2 - 60, g_h / 2 - 10, "Press SPACE to serve");
            }
        }

        
        HDC hdc = GetDC(g_hwnd);
        BitBlt(hdc, 0, 0, g_w, g_h, g_memDC, 0, 0, SRCCOPY);
        ReleaseDC(g_hwnd, hdc);

        
        if (dt < target_dt) {
            DWORD ms = (DWORD)((target_dt - dt) * 1000.0);
            if (ms > 0) Sleep(ms);
        }
    }

    DestroyBackbuffer();
    return 0;
}
