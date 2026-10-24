#pragma once

#include <windows.h>
#include <commctrl.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "shares.h"

// Custom messages posted from the worker thread (or the ListView subclass) to
// the UI thread.
#define WM_APP_CLEAR          (WM_APP + 1)
#define WM_APP_SHARE_RESULT   (WM_APP + 2) // wParam = row index
#define WM_APP_DONE           (WM_APP + 3)
#define WM_APP_COLUMN_RESIZED (WM_APP + 4) // posted when a column is resized

class App {
public:
    static App* instance();

    bool init(HINSTANCE hInst);
    int run();

    // Column indices (left to right).
    enum Column {
        ColShare = 0,
        ColTotal,
        ColUsed,
        ColFree,
        ColFreePct,
        ColCount
    };

private:
    App() = default;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    // Subclass procedure for the ListView: catches HDN_ENDADJUST (column
    // resize) from the header control and forwards it to the main window.
    static LRESULT CALLBACK ListSubclassProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);

    void onCreate(HWND hwnd);
    void onSize();
    void onCommand(WPARAM wParam);
    // Returns the CDRF_* flags for NM_CUSTOMDRAW (0 for other notifications).
    LRESULT onNotify(NMHDR* pnm);
    LRESULT onCustomDraw(NMLVCUSTOMDRAW* pcd);
    // Custom draw for the ListView's header control: paints each column header
    // with a background 10% darker than the default for better visibility.
    // Returns the CDRF_* flags.
    LRESULT onHeaderCustomDraw(NMCUSTOMDRAW* pcd);
    void onItemSelected(int index);
    void updateStatusSummary();

    void buildMenu();
    void createListView();
    void populateList(const std::vector<std::wstring>& shares);
    void updateRow(int index, const rdsmon::ShareResult& r);

    void refresh();
    void startWorker();
    void stopWorker();
    void showEditDialog();
    void showSettings();
    void showAbout();

    // Shown at startup when the config file was corrupt (not valid JSON): warns
    // the user and asks permission to overwrite the file with the new data. If
    // they decline, the file is left unchanged and the in-memory data is lost on
    // exit.
    void promptToRewriteCorruptConfig();

    // Persists UI state to the config file.
    void saveColumnWidths();
    void saveWindowSizeAndPosition();

    // (Re)starts the auto-refresh timer using config_.autoRefreshMinutes.
    void restartAutoRefreshTimer();

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND hwndList_ = nullptr;
    HWND hwndStatus_ = nullptr;
    HMENU hMenu_ = nullptr;
    WNDPROC origListProc_ = nullptr;

    rdsmon::Config config_;
    std::vector<rdsmon::ShareResult> results_;
    std::mutex resultsMutex_;

    std::atomic<bool> workerRunning_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread workerThread_;

    // True while the window is minimized; used to trigger an immediate refresh
    // when the window is restored or maximized.
    bool wasMinimized_ = false;
};
