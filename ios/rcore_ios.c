// Raylib platform backend for iOS.
// UIKit manages window creation, GL context, input, and frame timing.
// This file is #include'd by rcore.c (via CMake patching) so it shares
// the CoreData CORE global and all internal raylib types.

#include <sys/time.h>
#include <time.h>
#include <dlfcn.h>

// ---------------------------------------------------------------------------
// Core platform functions (called from rcore.c)
// ---------------------------------------------------------------------------

// Proc loader for rlLoadExtensions — iOS links GL symbols directly
static void *iosGLGetProcAddress(const char *name)
{
    return dlsym(RTLD_DEFAULT, name);
}

int InitPlatform(void)
{
    // On iOS, UIKit creates the GL context before raylib init.
    // Set currentFbo dimensions so rlglInit() and SetupViewport() use
    // the correct viewport size (otherwise they get 0x0 → blank screen).
    CORE.Window.currentFbo.width = CORE.Window.screen.width;
    CORE.Window.currentFbo.height = CORE.Window.screen.height;

    CORE.Window.ready = true;

    // Load GL extensions (VAO, instancing, etc.) — required on ES2
    rlLoadExtensions(iosGLGetProcAddress);

    TRACELOG(LOG_INFO, "PLATFORM: iOS initialized (%dx%d)",
            CORE.Window.currentFbo.width, CORE.Window.currentFbo.height);

    return 0;
}

void ClosePlatform(void)
{
}

double GetTime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double time = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
    static double baseTime = 0.0;
    if (baseTime == 0.0) baseTime = time;
    return time - baseTime;
}

void SwapScreenBuffer(void)
{
    // UIKit/CADisplayLink handles buffer presentation
}

void PollInputEvents(void)
{
    // UIKit touch events are handled by GameViewController
}

// WaitTime() is defined in rcore.c itself (not platform-specific)

// ---------------------------------------------------------------------------
// Window management stubs (iOS is always fullscreen)
// ---------------------------------------------------------------------------

bool WindowShouldClose(void) { return false; }
void ToggleFullscreen(void) { }
void ToggleBorderlessWindowed(void) { }
void MaximizeWindow(void) { }
void MinimizeWindow(void) { }
void RestoreWindow(void) { }
void SetWindowState(unsigned int flags) { (void)flags; }
void ClearWindowState(unsigned int flags) { (void)flags; }
void SetWindowIcon(Image image) { (void)image; }
void SetWindowIcons(Image *images, int count) { (void)images; (void)count; }
void SetWindowTitle(const char *title) { (void)title; }
void SetWindowPosition(int x, int y) { (void)x; (void)y; }
void SetWindowMonitor(int monitor) { (void)monitor; }
void SetWindowMinSize(int width, int height) { (void)width; (void)height; }
void SetWindowMaxSize(int width, int height) { (void)width; (void)height; }
void SetWindowSize(int width, int height) { (void)width; (void)height; }
void SetWindowOpacity(float opacity) { (void)opacity; }
void SetWindowFocused(void) { }
void *GetWindowHandle(void) { return NULL; }

int GetMonitorCount(void) { return 1; }
int GetCurrentMonitor(void) { return 0; }
int GetMonitorWidth(int monitor) { (void)monitor; return 0; }
int GetMonitorHeight(int monitor) { (void)monitor; return 0; }
int GetMonitorPhysicalWidth(int monitor) { (void)monitor; return 0; }
int GetMonitorPhysicalHeight(int monitor) { (void)monitor; return 0; }
int GetMonitorRefreshRate(int monitor) { (void)monitor; return 60; }
const char *GetMonitorName(int monitor) { (void)monitor; return "iOS Device"; }

Vector2 GetWindowPosition(void) { return (Vector2){0, 0}; }
Vector2 GetWindowScaleDPI(void) { return (Vector2){1, 1}; }

void SetClipboardText(const char *text) { (void)text; }
const char *GetClipboardText(void) { return ""; }
Image GetClipboardImage(void) { return (Image){0}; }

void ShowCursor(void) { }
void HideCursor(void) { }
void EnableCursor(void) { }
void DisableCursor(void) { }

void OpenURL(const char *url) { (void)url; }

int SetGamepadMappings(const char *mappings) { (void)mappings; return 0; }
void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor, float duration)
{
    (void)gamepad; (void)leftMotor; (void)rightMotor; (void)duration;
}

void SetMousePosition(int x, int y) { (void)x; (void)y; }
void SetMouseCursor(int cursor) { (void)cursor; }
const char *GetKeyName(int key) { (void)key; return ""; }
