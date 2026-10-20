#include "editdialog.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <cwctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

// This SDK's windowsx.h does not define the owner-draw item-action constants.
#ifndef DA_DRAWN
#define DA_DRAWN 1
#endif
#ifndef DA_REDO
#define DA_REDO 2
#endif
#ifndef DA_SKIP
#define DA_SKIP 3
#endif

namespace {

const wchar_t* kDialogClass = L"RDSMEditDialog";

enum : UINT {
    IDC_List = 1000,
    IDC_Edit,
    IDC_Add,
    IDC_Update,
    IDC_Remove,
    IDC_MoveUp,
    IDC_MoveDown,
    IDC_Save,
    IDC_Cancel,
};

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

// This SDK's commctrl.h does not define the ListBox_* convenience macros, so
// we issue the underlying LB_* messages directly.
int lbGetCurSel(HWND h) {
    return static_cast<int>(SendMessageW(h, LB_GETCURSEL, 0, 0));
}

void lbSetCurSel(HWND h, int sel) {
    SendMessageW(h, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
}

void lbReset(HWND h) {
    SendMessageW(h, LB_RESETCONTENT, 0, 0);
}

void lbAddString(HWND h, const wchar_t* s) {
    SendMessageW(h, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
}

// Lowercase copy, for case-insensitive share-name comparison (Windows share
// paths and drive letters are case-insensitive).
std::wstring toLower(const std::wstring& s) {
    std::wstring r(s);
    for (wchar_t& c : r) {
        c = static_cast<wchar_t>(::towlower(static_cast<wint_t>(c)));
    }
    return r;
}

// Indices that are the first occurrence of a share name appearing more than
// once (case-insensitive). These are drawn in red to flag the duplicate.
std::vector<bool> findDuplicateFirst(const std::vector<std::wstring>& shares) {
    std::vector<bool> red(shares.size(), false);
    std::map<std::wstring, std::pair<int, int>> firstAndCount; // name -> {firstIndex, count}
    for (int i = 0; i < static_cast<int>(shares.size()); ++i) {
        auto& p = firstAndCount[toLower(shares[i])];
        if (p.second == 0) {
            p.first = i;
        }
        ++p.second;
    }
    for (const auto& kv : firstAndCount) {
        if (kv.second.second > 1) {
            red[kv.second.first] = true;
        }
    }
    return red;
}

} // namespace

bool EditDialog::show(HWND parent, const std::vector<std::wstring>& initial,
                      std::vector<std::wstring>& outShares) {
    EditDialog dlg;
    dlg.hwndParent_ = parent;
    dlg.hInst_ = GetModuleHandleW(nullptr);
    dlg.shares_ = initial;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &EditDialog::WndProc;
    wc.hInstance = dlg.hInst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kDialogClass;
    RegisterClassExW(&wc);

    dlg.hwnd_ = CreateWindowExW(
        WS_EX_CONTROLPARENT, kDialogClass, L"Edit Shares",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 440,
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
        outShares = dlg.shares_;
        return true;
    }
    return false;
}

LRESULT CALLBACK EditDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    EditDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<EditDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<EditDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->handleMsg(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT EditDialog::handleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
        case WM_MEASUREITEM:
            onMeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam));
            return 0;
        case WM_DRAWITEM:
            return onDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        case WM_CLOSE:
            // The title-bar X is a close path too; block it while duplicates remain.
            if (!hasDuplicates()) {
                close(false);
            }
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

void EditDialog::onCreate(HWND hwnd) {
    hwnd_ = hwnd;

    // Use the system message font (lfMessageFont) for all controls. A hand-built
    // Segoe UI font (CreateFontW) renders heavier/bold on this machine for buttons,
    // statics and list boxes, while the system message font renders at the normal
    // weight (matching the edit control and the About dialog's button).
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfWeight = FW_NORMAL;
    hFont_ = CreateFontIndirectW(&ncm.lfMessageFont);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    // Owner-draw so the first instance of a duplicated share can be shown in red.
    hwndList_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
                                    LBS_OWNERDRAWFIXED,
                                0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(IDC_List),
                                hInst_, nullptr);
    hwndEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(IDC_Edit),
                                hInst_, nullptr);

    HWND btnAdd = CreateWindowExW(0, L"BUTTON", L"Add",
                                  WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                  hwnd_, reinterpret_cast<HMENU>(IDC_Add), hInst_, nullptr);
    HWND btnUpdate = CreateWindowExW(0, L"BUTTON", L"Update",
                                     WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                     hwnd_, reinterpret_cast<HMENU>(IDC_Update), hInst_, nullptr);
    HWND btnRemove = CreateWindowExW(0, L"BUTTON", L"Remove",
                                     WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                     hwnd_, reinterpret_cast<HMENU>(IDC_Remove), hInst_, nullptr);
    HWND btnMoveUp = CreateWindowExW(0, L"BUTTON", L"Move Up",
                                     WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                     hwnd_, reinterpret_cast<HMENU>(IDC_MoveUp), hInst_, nullptr);
    HWND btnMoveDown = CreateWindowExW(0, L"BUTTON", L"Move Down",
                                       WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                       hwnd_, reinterpret_cast<HMENU>(IDC_MoveDown), hInst_, nullptr);
    HWND btnSave = CreateWindowExW(0, L"BUTTON", L"Save",
                                   WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                   hwnd_, reinterpret_cast<HMENU>(IDC_Save), hInst_, nullptr);
    HWND btnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                     WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                     hwnd_, reinterpret_cast<HMENU>(IDC_Cancel), hInst_, nullptr);

    for (HWND c : {hwndList_, hwndEdit_, btnAdd, btnUpdate, btnRemove,
                   btnMoveUp, btnMoveDown, btnSave, btnCancel}) {
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    }

    // Compute the duplicate marking before populating the list so items are
    // drawn with the correct (red) color from the first paint.
    recomputeDuplicates();
    syncList();
    updateMoveButtons();
    updateCloseButtons();
}

void EditDialog::onSize() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int w = rc.right;
    const int h = rc.bottom;

    const int margin = 12;
    const int gap = 8;
    const int buttonH = 26;
    const int editH = 24;
    const int buttonW = 84;

    int bottomRowY = h - margin - buttonH;
    int midRowY = bottomRowY - gap - buttonH;
    int editY = midRowY - gap - editH;
    int listTop = margin;
    int listBottom = editY - gap;

    MoveWindow(hwndList_, margin, listTop, w - 2 * margin, listBottom - listTop, TRUE);
    MoveWindow(hwndEdit_, margin, editY, w - 2 * margin, editH, TRUE);

    MoveWindow(GetDlgItem(hwnd_, IDC_Add), margin, midRowY, buttonW, buttonH, TRUE);
    MoveWindow(GetDlgItem(hwnd_, IDC_Update), margin + buttonW + gap, midRowY, buttonW, buttonH, TRUE);
    MoveWindow(GetDlgItem(hwnd_, IDC_Remove), margin + 2 * (buttonW + gap), midRowY, buttonW, buttonH, TRUE);
    MoveWindow(GetDlgItem(hwnd_, IDC_MoveUp), margin + 3 * (buttonW + gap), midRowY, buttonW, buttonH, TRUE);
    MoveWindow(GetDlgItem(hwnd_, IDC_MoveDown), margin + 4 * (buttonW + gap), midRowY, buttonW, buttonH, TRUE);

    MoveWindow(GetDlgItem(hwnd_, IDC_Cancel), w - margin - buttonW, bottomRowY, buttonW, buttonH, TRUE);
    MoveWindow(GetDlgItem(hwnd_, IDC_Save), w - margin - 2 * (buttonW + gap), bottomRowY, buttonW, buttonH, TRUE);
}

void EditDialog::onCommand(WPARAM wParam) {
    UINT id = LOWORD(wParam);
    UINT code = HIWORD(wParam);

    if (id == IDC_List && code == LBN_SELCHANGE) {
        int sel = lbGetCurSel(hwndList_);
        if (sel >= 0 && sel < static_cast<int>(shares_.size())) {
            SetWindowTextW(hwndEdit_, shares_[sel].c_str());
        }
        updateMoveButtons();
        return;
    }

    switch (id) {
        case IDC_Add: {
            std::wstring text = trim(getEditText(hwndEdit_));
            if (!text.empty()) {
                shares_.push_back(text);
                syncList();
                lbSetCurSel(hwndList_, static_cast<int>(shares_.size()) - 1);
                SetWindowTextW(hwndEdit_, L"");
                recomputeDuplicates();
                updateMoveButtons();
                updateCloseButtons();
            }
            break;
        }
        case IDC_Update: {
            int sel = lbGetCurSel(hwndList_);
            std::wstring text = trim(getEditText(hwndEdit_));
            if (sel >= 0 && sel < static_cast<int>(shares_.size()) && !text.empty()) {
                shares_[sel] = text;
                syncList();
                lbSetCurSel(hwndList_, sel);
                recomputeDuplicates();
                updateMoveButtons();
                updateCloseButtons();
            }
            break;
        }
        case IDC_Remove: {
            int sel = lbGetCurSel(hwndList_);
            if (sel >= 0 && sel < static_cast<int>(shares_.size())) {
                shares_.erase(shares_.begin() + sel);
                syncList();
                SetWindowTextW(hwndEdit_, L"");
                recomputeDuplicates();
                updateMoveButtons();
                updateCloseButtons();
            }
            break;
        }
        case IDC_MoveUp: {
            int sel = lbGetCurSel(hwndList_);
            if (sel > 0) {
                std::swap(shares_[sel], shares_[sel - 1]);
                syncList();
                lbSetCurSel(hwndList_, sel - 1);
                SetWindowTextW(hwndEdit_, shares_[sel - 1].c_str());
                recomputeDuplicates();
                updateMoveButtons();
                updateCloseButtons();
            }
            break;
        }
        case IDC_MoveDown: {
            int sel = lbGetCurSel(hwndList_);
            if (sel >= 0 && sel + 1 < static_cast<int>(shares_.size())) {
                std::swap(shares_[sel], shares_[sel + 1]);
                syncList();
                lbSetCurSel(hwndList_, sel + 1);
                SetWindowTextW(hwndEdit_, shares_[sel + 1].c_str());
                recomputeDuplicates();
                updateMoveButtons();
                updateCloseButtons();
            }
            break;
        }
        case IDC_Save:
            // Block closing while any duplicate (red) entry remains.
            if (!hasDuplicates()) {
                close(true);
            }
            break;
        case IDC_Cancel:
            if (!hasDuplicates()) {
                close(false);
            }
            break;
    }
}

void EditDialog::syncList() {
    int sel = lbGetCurSel(hwndList_);
    lbReset(hwndList_);
    for (const auto& s : shares_) {
        lbAddString(hwndList_, s.c_str());
    }
    if (sel >= 0 && sel < static_cast<int>(shares_.size())) {
        lbSetCurSel(hwndList_, sel);
    }
}

void EditDialog::recomputeDuplicates() {
    dupFirst_ = findDuplicateFirst(shares_);
    // Force the listbox to redraw so the red marking updates (no erase: we fill
    // the item background ourselves in onDrawItem).
    InvalidateRect(hwndList_, nullptr, FALSE);
}

bool EditDialog::hasDuplicates() const {
    for (bool b : dupFirst_) {
        if (b) {
            return true;
        }
    }
    return false;
}

void EditDialog::updateMoveButtons() {
    const int sel = lbGetCurSel(hwndList_);
    const int n = static_cast<int>(shares_.size());
    // Move Up is enabled only when a non-top item is selected; Move Down only
    // when a non-bottom item is selected. With 0 or 1 shares (or no selection)
    // both are therefore disabled.
    const bool canUp = (sel > 0);
    const bool canDown = (sel >= 0 && sel < n - 1);
    EnableWindow(GetDlgItem(hwnd_, IDC_MoveUp), canUp ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd_, IDC_MoveDown), canDown ? TRUE : FALSE);
}

void EditDialog::updateCloseButtons() {
    // While any duplicate (red) entry remains the dialog cannot be closed, so
    // gray out Save, Cancel, and the title-bar X. When there are no duplicates
    // all three are enabled (Cancel / X return to the previous configuration).
    const BOOL enabled = hasDuplicates() ? FALSE : TRUE;
    EnableWindow(GetDlgItem(hwnd_, IDC_Save), enabled);
    EnableWindow(GetDlgItem(hwnd_, IDC_Cancel), enabled);
    // The title-bar X can't be disabled directly; gray it via the system menu.
    if (HMENU sysMenu = GetSystemMenu(hwnd_, FALSE)) {
        EnableMenuItem(sysMenu, SC_CLOSE, enabled ? MF_ENABLED : MF_GRAYED);
    }
}

void EditDialog::onMeasureItem(MEASUREITEMSTRUCT* p) {
    if (p->CtlID != IDC_List) {
        return;
    }
    HDC hdc = GetDC(hwnd_);
    HFONT old = static_cast<HFONT>(SelectObject(hdc, hFont_));
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(hwnd_, hdc);
    p->itemHeight = tm.tmHeight + tm.tmExternalLeading + 4;
}

int EditDialog::onDrawItem(DRAWITEMSTRUCT* p) {
    if (p->CtlID != IDC_List) {
        return DA_REDO;
    }
    const int idx = static_cast<int>(p->itemID);
    if (idx < 0 || idx >= static_cast<int>(shares_.size())) {
        return DA_REDO;
    }
    const bool selected = (p->itemState & ODS_SELECTED) != 0;
    const bool red = (idx < static_cast<int>(dupFirst_.size())) && dupFirst_[idx];

    const HBRUSH bg = selected ? reinterpret_cast<HBRUSH>(COLOR_HIGHLIGHT + 1)
                               : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    FillRect(p->hDC, &p->rcItem, bg);

    COLORREF textColor;
    if (red) {
        textColor = RGB(200, 0, 0);
    } else if (selected) {
        textColor = GetSysColor(COLOR_HIGHLIGHTTEXT);
    } else {
        textColor = GetSysColor(COLOR_WINDOWTEXT);
    }
    SetTextColor(p->hDC, textColor);
    SetBkMode(p->hDC, TRANSPARENT);

    RECT rc = p->rcItem;
    rc.left += 4;
    DrawTextW(p->hDC, shares_[idx].c_str(), -1, &rc,
              DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);
    return DA_DRAWN;
}

void EditDialog::close(bool saved) {
    saved_ = saved;
    DestroyWindow(hwnd_);
}
