#pragma once

#include <windows.h>

#include <string>
#include <vector>

// A modal dialog for adding, editing, and removing entries in the share list.
class EditDialog {
public:
    // Displays the dialog modally. Returns true if the user clicked Save.
    // On save, outShares is set to the edited list.
    static bool show(HWND parent, const std::vector<std::wstring>& initial,
                     std::vector<std::wstring>& outShares);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMsg(HWND, UINT, WPARAM, LPARAM);

    void onCreate(HWND hwnd);
    void onSize();
    void onCommand(WPARAM wParam);
    void onMeasureItem(MEASUREITEMSTRUCT* p);
    int onDrawItem(DRAWITEMSTRUCT* p);
    void syncList();
    void recomputeDuplicates();
    bool hasDuplicates() const;
    void updateMoveButtons();
    // Enables/disables Save, Cancel, and the title-bar X based on whether any
    // duplicate (red) entry remains. While duplicates exist all three are
    // disabled (grayed out) so the dialog cannot be closed.
    void updateCloseButtons();
    void close(bool saved);

    HWND hwnd_ = nullptr;
    HWND hwndParent_ = nullptr;
    HWND hwndList_ = nullptr;
    HWND hwndEdit_ = nullptr;
    HINSTANCE hInst_ = nullptr;
    HFONT hFont_ = nullptr;

    std::vector<std::wstring> shares_;
    // Parallel to shares_: true where this index is the first occurrence of a
    // share name that appears more than once (case-insensitive). Drawn in red.
    std::vector<bool> dupFirst_;
    bool saved_ = false;
};
