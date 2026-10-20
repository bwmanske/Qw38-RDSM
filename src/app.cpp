#include "app.h"
#include "aboutdialog.h"
#include "editdialog.h"
#include "settingsdialog.h"

#include <windows.h>
#include <commctrl.h>
#include "gdiplus_prelude.h"
#include "resources/resources.h"

#include <string>
#include <vector>

using namespace Gdiplus;

namespace {

const wchar_t* kWindowClass = L"RDSMMainWindow";
const wchar_t* kWindowTitle = L"Remote Disk Space Monitor";

struct ColumnInfo {
    const wchar_t* title;
    int width;
    int fmt;
};

const ColumnInfo kColumns[App::ColCount] = {
    {L"Share Name", 280, LVCFMT_LEFT},
    {L"Total Size", 110, LVCFMT_RIGHT},
    {L"Used Space", 110, LVCFMT_RIGHT},
    {L"Free Space", 110, LVCFMT_RIGHT},
    {L"Free %",     130, LVCFMT_LEFT},
};

enum : UINT {
    IDM_Refresh = 100,
    IDM_EditShares,
    IDM_Settings,
    IDM_Exit,
    IDM_About,
};

// Timer ID for the auto-refresh timer.
const UINT_PTR IDT_AutoRefresh = 1;

// This SDK's ListView_SetItemText macro assigns the text to LV_ITEM.pszText
// (a non-const LPWSTR), which rejects string literals under /Zc:strictStrings.
// These helpers set text via an LVITEMW instead. The const_cast is safe: the
// list view only reads pszText.
int lvInsertItem(HWND list, int index, const wchar_t* text) {
    LVITEMW lvi{};
    lvi.mask = LVIF_TEXT;
    lvi.iItem = index;
    lvi.pszText = const_cast<LPWSTR>(text);
    lvi.cchTextMax = 256;
    return ListView_InsertItem(list, &lvi);
}

void lvSetItemText(HWND list, int index, int subitem, const wchar_t* text) {
    LVITEMW lvi{};
    lvi.mask = LVIF_TEXT;
    lvi.iItem = index;
    lvi.iSubItem = subitem;
    lvi.pszText = const_cast<LPWSTR>(text);
    lvi.cchTextMax = 256;
    ListView_SetItem(list, &lvi);
}

} // namespace

App* App::instance() {
    static App app;
    return &app;
}

bool App::init(HINSTANCE hInst) {
    hInst_ = hInst;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &App::WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    if (!RegisterClassExW(&wc)) {
        return false;
    }
    return true;
}

int App::run() {
    // Load the config before creating the window so the saved size and
    // position can be applied at creation time.
    config_ = rdsmon::loadConfig();

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 940, h = 540;
    if (config_.hasWindowPosition) {
        x = config_.windowX;
        y = config_.windowY;
    }
    if (config_.hasWindowSize) {
        w = config_.windowWidth;
        h = config_.windowHeight;
    }

    hwnd_ = CreateWindowExW(
        0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        nullptr, nullptr, hInst_, this);
    if (!hwnd_) {
        return 1;
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    stopWorker();
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->handleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            onCreate(hwnd);
            return 0;
        case WM_SIZE:
            onSize();
            // Suspend the auto-refresh timer while the window is minimized;
            // restart it (and refresh immediately) when it is restored or
            // maximized.
            switch (wParam) {
                case SIZE_MINIMIZED:
                    wasMinimized_ = true;
                    KillTimer(hwnd_, IDT_AutoRefresh);
                    break;
                case SIZE_RESTORED:
                case SIZE_MAXIMIZED:
                    if (wasMinimized_) {
                        wasMinimized_ = false;
                        refresh();
                        restartAutoRefreshTimer();
                    }
                    break;
            }
            return 0;
        case WM_TIMER:
            if (wParam == IDT_AutoRefresh) {
                refresh();
            }
            return 0;
        case WM_COMMAND:
            onCommand(wParam);
            return 0;
        case WM_NOTIFY:
            // For NM_CUSTOMDRAW the return value of WM_NOTIFY is the CDRF_*
            // flag the control acts on; it is not stored in the struct.
            return onNotify(reinterpret_cast<NMHDR*>(lParam));
        case WM_APP_CLEAR:
            populateList(config_.shares);
            return 0;
        case WM_APP_SHARE_RESULT: {
            int index = static_cast<int>(wParam);
            rdsmon::ShareResult r;
            {
                std::lock_guard<std::mutex> lk(resultsMutex_);
                if (index >= 0 && index < static_cast<int>(results_.size())) {
                    r = results_[index];
                }
            }
            updateRow(index, r);
            return 0;
        }
        case WM_APP_DONE:
            updateStatusSummary();
            return 0;
        case WM_APP_COLUMN_RESIZED:
            saveColumnWidths();
            return 0;
        case WM_EXITSIZEMOVE:
            // Sent when the user finishes resizing or moving the window.
            saveWindowSizeAndPosition();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, IDT_AutoRefresh);
            stopWorker();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void App::onCreate(HWND hwnd) {
    hwnd_ = hwnd;
    buildMenu();
    createListView();
    refresh();
    restartAutoRefreshTimer();
}

void App::onSize() {
    if (hwndStatus_) {
        SendMessageW(hwndStatus_, WM_SIZE, 0, 0);
    }
    if (hwndList_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        int statusH = 0;
        if (hwndStatus_) {
            RECT src{};
            GetWindowRect(hwndStatus_, &src);
            statusH = src.bottom - src.top;
        }
        MoveWindow(hwndList_, 0, 0, rc.right, rc.bottom - statusH, TRUE);
    }
}

void App::buildMenu() {
    hMenu_ = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_Refresh, L"&Refresh\tF5");
    AppendMenuW(fileMenu, MF_STRING, IDM_EditShares, L"&Edit Shares...");
    AppendMenuW(fileMenu, MF_STRING, IDM_Settings, L"&Settings...");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_Exit, L"E&xit");
    AppendMenuW(hMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");

    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(helpMenu, MF_STRING, IDM_About, L"&About");
    AppendMenuW(hMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"&Help");

    SetMenu(hwnd_, hMenu_);
}

void App::createListView() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);

    hwndList_ = CreateWindowExW(
        0, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, rc.right, rc.bottom,
        hwnd_, nullptr, hInst_, nullptr);

    ListView_SetExtendedListViewStyle(hwndList_,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    for (int i = 0; i < ColCount; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = const_cast<LPWSTR>(kColumns[i].title);
        // Use the saved width if present, otherwise the default.
        col.cx = (i < static_cast<int>(config_.columnWidths.size()))
                     ? config_.columnWidths[i]
                     : kColumns[i].width;
        col.fmt = kColumns[i].fmt;
        ListView_InsertColumn(hwndList_, i, &col);
    }

    // Columns are resizable by dragging the header dividers (default in report view).
    // Subclass the ListView to catch HDN_ENDADJUST (sent by the header control
    // when a column divider drag ends) so we can persist the new widths.
    origListProc_ = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwndList_, GWLP_WNDPROC));
    SetWindowLongPtrW(hwndList_, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(&App::ListSubclassProc));

    hwndStatus_ = CreateWindowExW(
        0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(1), hInst_, nullptr);
}

void App::populateList(const std::vector<std::wstring>& shares) {
    ListView_DeleteAllItems(hwndList_);
    for (std::size_t i = 0; i < shares.size(); ++i) {
        int idx = lvInsertItem(hwndList_, static_cast<int>(i), shares[i].c_str());
        if (idx >= 0) {
            lvSetItemText(hwndList_, idx, ColTotal, L"Querying...");
            lvSetItemText(hwndList_, idx, ColUsed, L"");
            lvSetItemText(hwndList_, idx, ColFree, L"");
            lvSetItemText(hwndList_, idx, ColFreePct, L"");
        }
    }
}

void App::updateRow(int index, const rdsmon::ShareResult& r) {
    if (index < 0) {
        return;
    }
    if (r.ok) {
        lvSetItemText(hwndList_, index, ColTotal, rdsmon::formatBytes(r.totalBytes).c_str());
        lvSetItemText(hwndList_, index, ColUsed, rdsmon::formatBytes(r.usedBytes).c_str());
        lvSetItemText(hwndList_, index, ColFree, rdsmon::formatBytes(r.freeBytes).c_str());
        lvSetItemText(hwndList_, index, ColFreePct, rdsmon::formatPercent(r.freePercent).c_str());
    } else {
        // "Offline" goes in the column just right of the share name.
        lvSetItemText(hwndList_, index, ColTotal, L"Offline");
        lvSetItemText(hwndList_, index, ColUsed, L"");
        lvSetItemText(hwndList_, index, ColFree, L"");
        lvSetItemText(hwndList_, index, ColFreePct, L"");
    }
}

void App::onCommand(WPARAM wParam) {
    switch (LOWORD(wParam)) {
        case IDM_Refresh:
            refresh();
            break;
        case IDM_EditShares:
            showEditDialog();
            break;
        case IDM_Settings:
            showSettings();
            break;
        case IDM_Exit:
            DestroyWindow(hwnd_);
            break;
        case IDM_About:
            showAbout();
            break;
    }
}

LRESULT App::onNotify(NMHDR* pnm) {
    if (pnm->hwndFrom != hwndList_) {
        return 0;
    }
    switch (pnm->code) {
        case NM_CUSTOMDRAW:
            return onCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(pnm));
        case LVN_ITEMCHANGED: {
            auto* lvi = reinterpret_cast<NMLISTVIEW*>(pnm);
            if (lvi->uNewState & LVIS_SELECTED) {
                onItemSelected(static_cast<int>(lvi->iItem));
            }
            break;
        }
    }
    return 0;
}

LRESULT App::onCustomDraw(NMLVCUSTOMDRAW* pcd) {
    // Custom draw is a two-stage handshake (see NM_CUSTOMDRAW docs):
    //   CDDS_PREPAINT            -> return CDRF_NOTIFYITEMDRAW
    //   CDDS_ITEMPREPAINT        -> return CDRF_NOTIFYSUBITEMDRAW
    //   CDDS_ITEMPREPAINT|SUBITEM-> draw the subitem, return CDRF_SKIPDEFAULT/DODEFAULT
    // CDRF_NOTIFYITEMDRAW and CDRF_NOTIFYSUBITEMDRAW are the same value (0x20);
    // the control distinguishes them by the draw stage they are returned at.
    switch (pcd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            if (pcd->iSubItem != ColFreePct) {
                return CDRF_DODEFAULT;
            }
            int row = static_cast<int>(pcd->nmcd.dwItemSpec);
            rdsmon::ShareResult r;
            {
                std::lock_guard<std::mutex> lk(resultsMutex_);
                if (row >= 0 && row < static_cast<int>(results_.size())) {
                    r = results_[row];
                }
            }
            if (!r.ok) {
                return CDRF_DODEFAULT; // draw "Offline"
            }

            RECT rc = pcd->nmcd.rc;
            Graphics g(pcd->nmcd.hdc);

            const int pad = 4;
            int x = rc.left + pad;
            int y = rc.top + pad;
            int w = rc.right - rc.left - 2 * pad;
            int h = rc.bottom - rc.top - 2 * pad;

            // Track background.
            SolidBrush trackBrush(Color(235, 235, 235));
            g.FillRectangle(&trackBrush, x, y, w, h);

            // Filled portion, colored by how much space is free.
            Color fillColor;
            if (r.freePercent > 50.0) {
                fillColor = Color(76, 175, 80);   // green
            } else if (r.freePercent > 20.0) {
                fillColor = Color(255, 193, 7);   // amber
            } else {
                fillColor = Color(229, 115, 115); // red
            }
            int fillW = static_cast<int>(w * (r.freePercent / 100.0));
            if (fillW > 0) {
                SolidBrush fillBrush(fillColor);
                g.FillRectangle(&fillBrush, x, y, fillW, h);
            }

            // Percentage text on top.
            std::wstring text = rdsmon::formatPercent(r.freePercent);
            Font font(L"Segoe UI", 9.0f, FontStyleRegular, UnitPoint);
            SolidBrush textBrush(Color(0, 0, 0));
            StringFormat fmt;
            fmt.SetAlignment(StringAlignmentCenter);
            fmt.SetLineAlignment(StringAlignmentCenter);
            RectF textRect(static_cast<REAL>(x), static_cast<REAL>(y),
                           static_cast<REAL>(w), static_cast<REAL>(h));
            g.DrawString(text.c_str(), -1, &font, textRect, &fmt, &textBrush);

            return CDRF_SKIPDEFAULT;
        }
        default:
            return CDRF_DODEFAULT;
    }
}

LRESULT App::onHeaderCustomDraw(NMCUSTOMDRAW* pcd) {
    // This SDK's header control reports its custom-draw stages using the CDDS_*
    // constants (the same family the list view uses), not the HDDS_* values from
    // the docs: CDDS_PREPAINT (0x1) then CDDS_ITEMPREPAINT (0x10001).
    //   CDDS_PREPAINT     -> return CDRF_NOTIFYITEMDRAW
    //   CDDS_ITEMPREPAINT -> draw the item, return CDRF_SKIPDEFAULT
    switch (pcd->dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: {
            const int col = static_cast<int>(pcd->dwItemSpec);
            if (col < 0 || col >= ColCount) {
                return CDRF_DODEFAULT;
            }

            // Paint the header background 10% darker than the default face color
            // so the column titles stand out more.
            const COLORREF base = GetSysColor(COLOR_3DFACE);
            const COLORREF darker = RGB(GetRValue(base) * 90 / 100,
                                        GetGValue(base) * 90 / 100,
                                        GetBValue(base) * 90 / 100);
            const HBRUSH bg = CreateSolidBrush(darker);
            FillRect(pcd->hdc, &pcd->rc, bg);
            DeleteObject(bg);

            // Column title, aligned to match the column format.
            wchar_t buf[64] = {};
            LVCOLUMNW lvc{};
            lvc.mask = LVCF_FMT | LVCF_TEXT;
            lvc.pszText = buf;
            lvc.cchTextMax = 64;
            UINT fmt = LVCFMT_LEFT;
            if (ListView_GetColumn(hwndList_, col, &lvc)) {
                fmt = lvc.fmt;
            }
            SetTextColor(pcd->hdc, GetSysColor(COLOR_WINDOWTEXT));
            SetBkMode(pcd->hdc, TRANSPARENT);
            RECT rc = pcd->rc;
            rc.left += 4;
            rc.right -= 4;
            UINT dtFlags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
            if (fmt == LVCFMT_RIGHT) {
                dtFlags |= DT_RIGHT;
            } else if (fmt == LVCFMT_CENTER) {
                dtFlags |= DT_CENTER;
            } else {
                dtFlags |= DT_LEFT;
            }
            DrawTextW(pcd->hdc, buf, -1, &rc, dtFlags);

            // Redraw the column divider (a thin line at the item's right edge)
            // since we are skipping the default draw.
            if (col + 1 < ColCount) {
                const COLORREF line = RGB(GetRValue(base) * 70 / 100,
                                          GetGValue(base) * 70 / 100,
                                          GetBValue(base) * 70 / 100);
                const HPEN pen = CreatePen(PS_SOLID, 1, line);
                const HPEN oldPen = static_cast<HPEN>(SelectObject(pcd->hdc, pen));
                const int x = pcd->rc.right - 1;
                MoveToEx(pcd->hdc, x, pcd->rc.top, nullptr);
                LineTo(pcd->hdc, x, pcd->rc.bottom);
                SelectObject(pcd->hdc, oldPen);
                DeleteObject(pen);
            }
            return CDRF_SKIPDEFAULT;
        }
        default:
            return CDRF_DODEFAULT;
    }
}

void App::onItemSelected(int index) {
    if (!hwndStatus_) {
        return;
    }
    rdsmon::ShareResult r;
    {
        std::lock_guard<std::mutex> lk(resultsMutex_);
        if (index >= 0 && index < static_cast<int>(results_.size())) {
            r = results_[index];
        }
    }
    if (!r.ok && !r.error.empty()) {
        SendMessageW(hwndStatus_, SB_SETTEXTW, 0,
                     reinterpret_cast<LPARAM>(r.error.c_str()));
    } else {
        updateStatusSummary();
    }
}

void App::updateStatusSummary() {
    if (!hwndStatus_) {
        return;
    }
    int total = static_cast<int>(config_.shares.size());
    int online = 0;
    {
        std::lock_guard<std::mutex> lk(resultsMutex_);
        for (const auto& r : results_) {
            if (r.ok) ++online;
        }
    }
    wchar_t buf[128];
    swprintf_s(buf, L"%d of %d shares online", online, total);
    SendMessageW(hwndStatus_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(buf));
}

void App::refresh() {
    startWorker();
}

void App::startWorker() {
    if (workerRunning_.load()) {
        return;
    }
    // Join the previous worker thread before starting a new one. workerRunning_
    // is false, so the prior thread has finished and join() returns immediately.
    // Without this, assigning a new std::thread to the still-joinable
    // workerThread_ calls std::terminate() and crashes the app.
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    stopRequested_.store(false);
    workerRunning_.store(true);

    std::vector<std::wstring> localShares = config_.shares;
    {
        std::lock_guard<std::mutex> lk(resultsMutex_);
        results_.assign(localShares.size(), rdsmon::ShareResult{});
    }

    HWND hwnd = hwnd_;
    workerThread_ = std::thread([this, hwnd, localShares] {
        PostMessageW(hwnd, WM_APP_CLEAR, 0, 0);
        for (std::size_t i = 0; i < localShares.size(); ++i) {
            rdsmon::ShareResult r = rdsmon::queryShare(localShares[i]);
            {
                std::lock_guard<std::mutex> lk(resultsMutex_);
                if (i < results_.size()) {
                    results_[i] = r;
                }
            }
            PostMessageW(hwnd, WM_APP_SHARE_RESULT, static_cast<WPARAM>(i), 0);
        }
        PostMessageW(hwnd, WM_APP_DONE, 0, 0);
        workerRunning_.store(false);
    });
}

void App::stopWorker() {
    stopRequested_.store(true);
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void App::showEditDialog() {
    stopWorker();
    std::vector<std::wstring> edited;
    if (EditDialog::show(hwnd_, config_.shares, edited)) {
        config_.shares = edited;
        rdsmon::saveConfig(config_);
        refresh();
    }
}

void App::showSettings() {
    stopWorker();
    int minutes = 0;
    if (SettingsDialog::show(hwnd_, config_.autoRefreshMinutes, minutes)) {
        config_.autoRefreshMinutes = minutes;
        rdsmon::saveConfig(config_);
        restartAutoRefreshTimer();
    }
}

void App::showAbout() {
    AboutDialog::show(hwnd_);
}

void App::restartAutoRefreshTimer() {
    if (!hwnd_) {
        return;
    }
    KillTimer(hwnd_, IDT_AutoRefresh);
    // Interval in milliseconds: minutes * 60 * 1000. The max (3600 minutes =
    // 216,000,000 ms) fits in the UINT uElapse parameter of SetTimer.
    UINT elapse = static_cast<UINT>(config_.autoRefreshMinutes) * 60u * 1000u;
    SetTimer(hwnd_, IDT_AutoRefresh, elapse, nullptr);
}

LRESULT CALLBACK App::ListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = App::instance();
    if (msg == WM_NOTIFY) {
        auto* nm = reinterpret_cast<NMHDR*>(lParam);
        // Notifications from the header control (its parent is the list view):
        // HDN_ENDTRACK (a column divider drag ended) and NM_CUSTOMDRAW (we paint
        // the column headers with a darker background).
        HWND header = ListView_GetHeader(hwnd);
        if (header && nm->hwndFrom == header) {
            if (nm->code == HDN_ENDTRACK) {
                PostMessageW(self->hwnd_, WM_APP_COLUMN_RESIZED, 0, 0);
            } else if (nm->code == NM_CUSTOMDRAW) {
                return self->onHeaderCustomDraw(reinterpret_cast<NMCUSTOMDRAW*>(nm));
            }
        }
    }
    return CallWindowProcW(self->origListProc_, hwnd, msg, wParam, lParam);
}

void App::saveColumnWidths() {
    if (!hwndList_) {
        return;
    }
    config_.columnWidths.clear();
    for (int i = 0; i < ColCount; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_WIDTH;
        if (ListView_GetColumn(hwndList_, i, &col)) {
            config_.columnWidths.push_back(col.cx);
        }
    }
    rdsmon::saveConfig(config_);
}

void App::saveWindowSizeAndPosition() {
    RECT rc{};
    GetWindowRect(hwnd_, &rc);
    config_.windowWidth = rc.right - rc.left;
    config_.windowHeight = rc.bottom - rc.top;
    config_.hasWindowSize = true;
    config_.windowX = rc.left;
    config_.windowY = rc.top;
    config_.hasWindowPosition = true;
    rdsmon::saveConfig(config_);
}
