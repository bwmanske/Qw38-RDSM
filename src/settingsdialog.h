#pragma once

#include <windows.h>

// A modal dialog for editing app settings. The only setting is the auto-refresh
// interval (in minutes, 1-3600).
class SettingsDialog {
public:
    // Displays the dialog modally. currentMinutes pre-populates the field.
    // Returns true if the user clicked Save with a valid value; on save,
    // outMinutes is set to the new value.
    static bool show(HWND parent, int currentMinutes, int& outMinutes);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMsg(HWND, UINT, WPARAM, LPARAM);

    void onCreate(HWND hwnd);
    void onSize();
    void onCommand(WPARAM wParam);
    void close(bool saved);

    HWND hwnd_ = nullptr;
    HWND hwndParent_ = nullptr;
    HWND hwndLabel_ = nullptr;
    HWND hwndEdit_ = nullptr;
    HINSTANCE hInst_ = nullptr;
    HFONT hFont_ = nullptr;

    int currentMinutes_ = 15;
    bool saved_ = false;
};
