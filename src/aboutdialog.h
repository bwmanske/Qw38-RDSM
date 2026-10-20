#pragma once

#include <windows.h>

namespace Gdiplus {
class Bitmap;
}

// A modal "About" dialog: the app image (RDSM_128.png) at the top center, the
// app name, the version, and the build date/time, plus an OK button.
class AboutDialog {
public:
    static void show(HWND parent);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMsg(HWND, UINT, WPARAM, LPARAM);

    void onCreate(HWND hwnd);
    void onPaint(HWND hwnd);
    void onCommand(WPARAM wParam);

    HWND hwnd_ = nullptr;
    HINSTANCE hInst_ = nullptr;
    Gdiplus::Bitmap* image_ = nullptr;
};
