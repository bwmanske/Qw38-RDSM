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
    // Runs the share error test (rdsmon::validateShares) over shares_ and stores
    // the per-line results in lineError_. Called on creation and after every
    // change to the list.
    void recomputeErrors();
    // True if any line has an error (malformed or duplicate).
    bool hasErrors() const;
    void updateMoveButtons();
    // Enables/disables Save, Cancel, and the title-bar X. Save is enabled only
    // when the current list has no error. Cancel and the title-bar X are enabled
    // only when the initially loaded (saved) list had no error — they return to
    // that saved state, which is a known-good state only if it passed validation.
    void updateButtons();
    // Sets the red error label (lower-left) to the error of the currently
    // selected listbox line, or blank when that line has no error / none is
    // selected.
    void updateErrorLabel();
    void close(bool saved);

    HWND hwnd_ = nullptr;
    HWND hwndParent_ = nullptr;
    HWND hwndList_ = nullptr;
    HWND hwndEdit_ = nullptr;
    HWND hwndError_ = nullptr;
    HINSTANCE hInst_ = nullptr;
    HFONT hFont_ = nullptr;

    std::vector<std::wstring> shares_;
    // Parallel to shares_: the error message for each line (empty = valid), as
    // reported by the share error test. Lines with a non-empty error are drawn
    // in red.
    std::vector<std::wstring> lineError_;
    // Whether the initially loaded (saved) list had an error, captured once on
    // creation. Cancel / the title-bar X return to that saved state, so they are
    // enabled only when this is false (the saved state is known-good).
    bool initialHasErrors_ = false;
    bool saved_ = false;
};
