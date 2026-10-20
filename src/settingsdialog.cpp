#include "settingsdialog.h"

#include <windows.h>

#include <string>

namespace {

const wchar_t* kDialogClass = L"RDSMSettingsDialog";

enum : UINT {
    IDC_AutoRefresh = 1000,
    IDC_Save,
    IDC_Cancel,
};

const int kMinMinutes = 1;
const int kMaxMinutes = 3600;

std::wstring trim(const std::wstring& s) {
    const wchar_t* ws = L" \t\r\n";
    std::size_t b = s.find_first_not_of(ws);
    if (b == std::wstring::npos) {
        return {};
    }
    std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::wstring getEditText(HWND edit) {
    int len = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<std::size_t>(len), L'\0');
    if (len > 0) {
        GetWindowTextW(edit, text.data(), len + 1);
    }
    return text;
}

} // namespace

bool SettingsDialog::show(HWND parent, int currentMinutes, int& outMinutes) {
    SettingsDialog dlg;
    dlg.hwndParent_ = parent;
    dlg.hInst_ = GetModuleHandleW(nullptr);
    dlg.currentMinutes_ = currentMinutes;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsDialog::WndProc;
    wc.hInstance = dlg.hInst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kDialogClass;
    RegisterClassExW(&wc);

    dlg.hwnd_ = CreateWindowExW(
        WS_EX_CONTROLPARENT, kDialogClass, L"Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 340, 150,
        parent, nullptr, dlg.hInst_, &dlg);
    if (!dlg.hwnd_) {
        return false;
    }

    EnableWindow(parent, FALSE);
    SetForegroundWindow(dlg.hwnd_);

    MSG msg{};
    while (dlg.hwnd_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg.hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    if (dlg.saved_) {
        outMinutes = dlg.currentMinutes_;
        return true;
    }
    return false;
}

LRESULT CALLBACK SettingsDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->handleMsg(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsDialog::handleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            onCreate(hwnd);
            return 0;
        case WM_SIZE:
            onSize();
            return 0;
        case WM_COMMAND:
            onCommand(wParam);
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            if (hFont_) {
                DeleteObject(hFont_);
                hFont_ = nullptr;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void SettingsDialog::onCreate(HWND hwnd) {
    hwnd_ = hwnd;

    // Use the system message font (lfMessageFont) for all controls. A hand-built
    // Segoe UI font (CreateFontW) renders heavier/bold on this machine for buttons
    // and statics, while the system message font renders at the normal weight
    // (matching the edit control and the About dialog's button).
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfWeight = FW_NORMAL;
    hFont_ = CreateFontIndirectW(&ncm.lfMessageFont);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hwndLabel_ = CreateWindowExW(0, L"STATIC", L"Auto Refresh (minutes):",
                                 WS_CHILD | WS_VISIBLE,
                                 0, 0, 10, 10, hwnd_, nullptr, hInst_, nullptr);

    wchar_t buf[16];
    swprintf_s(buf, L"%d", currentMinutes_);
    hwndEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buf,
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                                0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(IDC_AutoRefresh),
                                hInst_, nullptr);

    HWND btnSave = CreateWindowExW(0, L"BUTTON", L"Save",
                                   WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                   hwnd_, reinterpret_cast<HMENU>(IDC_Save), hInst_, nullptr);
    HWND btnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                     WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                     hwnd_, reinterpret_cast<HMENU>(IDC_Cancel), hInst_, nullptr);

    for (HWND c : {hwndLabel_, hwndEdit_, btnSave, btnCancel}) {
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    }
}

void SettingsDialog::onSize() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int w = rc.right;
    const int h = rc.bottom;

    const int margin = 12;
    const int gap = 8;
    const int buttonH = 26;
    const int editH = 24;
    const int buttonW = 84;
    const int labelH = 20;

    int labelY = margin;
    int editY = labelY + labelH + gap;
    int bottomRowY = h - margin - buttonH;

    MoveWindow(hwndLabel_, margin, labelY, w - 2 * margin, labelH, TRUE);
    MoveWindow(hwndEdit_, margin, editY, 120, editH, TRUE);

    MoveWindow(GetDlgItem(hwnd_, IDC_Cancel), w - margin - buttonW, bottomRowY, buttonW, buttonH, TRUE);
    MoveWindow(GetDlgItem(hwnd_, IDC_Save), w - margin - 2 * (buttonW + gap), bottomRowY, buttonW, buttonH, TRUE);
}

void SettingsDialog::onCommand(WPARAM wParam) {
    UINT id = LOWORD(wParam);
    switch (id) {
        case IDC_Save: {
            std::wstring text = trim(getEditText(hwndEdit_));
            if (text.empty()) {
                MessageBoxW(hwnd_, L"Please enter a value.", L"Settings",
                            MB_OK | MB_ICONWARNING);
                return;
            }
            wchar_t* end = nullptr;
            long value = wcstol(text.c_str(), &end, 10);
            if (end == text.c_str() || *end != L'\0') {
                MessageBoxW(hwnd_, L"Please enter a whole number.", L"Settings",
                            MB_OK | MB_ICONWARNING);
                return;
            }
            if (value < kMinMinutes || value > kMaxMinutes) {
                wchar_t buf[128];
                swprintf_s(buf, L"Please enter a value between %d and %d.",
                           kMinMinutes, kMaxMinutes);
                MessageBoxW(hwnd_, buf, L"Settings", MB_OK | MB_ICONWARNING);
                return;
            }
            currentMinutes_ = static_cast<int>(value);
            close(true);
            break;
        }
        case IDC_Cancel:
            close(false);
            break;
    }
}

void SettingsDialog::close(bool saved) {
    saved_ = saved;
    DestroyWindow(hwnd_);
}
