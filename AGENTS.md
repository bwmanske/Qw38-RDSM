# AGENTS.md

RDSM (Remote Disk Space Monitor): Win32 C++17 GUI app that queries free space on a list of UNC network shares and Windows drive letters. Port of `RDSM.ps1` (original PowerShell reference). Full context: `specs/RDSM spec.md`.

## Build

Run `.\build.ps1` from the project root (stops a running `RDSM.exe`, configures, builds Release, runs tests). Full options and the equivalent manual commands: `docs/BUILDING.md`.

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

- Toolchain: VS 18 Community (MSVC 19.51) + Windows SDK 10.0.26100. CMake auto-detects; no env setup needed.
- Output: `build\bin\RDSM.exe` (GUI app, `wWinMain`).
- Each build runs the `BuildInfo` custom target, regenerating `build\generated\build_info.h` (version + 12-hour build timestamp) before compiling. Version lives in `RDSM_VERSION` in CMakeLists.
- No linter. GUI behavior is verified by running the exe.

## Tests

```
ctest --test-dir build -C Release --output-on-failure
```

- CTest runs the `RDSMTests` target (`build\bin\Release\RDSMTests.exe`), a dependency-free harness (`tests/test_framework.h`: `TEST`/`CHECK`/`CHECK_EQ`) that unit-tests the non-GUI logic in `src/shares.cpp` (formatting, `queryShare`, config load/save). Full docs: `docs/TESTING.md`.
- Config tests redirect `APPDATA` to a temp dir (never touch the real config); `queryShare` tests use the local `C:` drive and a missing path (no network).
- Built by default; disable with `-DBUILD_TESTING=OFF`. GUI code (app/editdialog/aboutdialog) is not unit tested.

## SDK gotchas (standard Windows SDK 10.0.26100 behaviors that need workarounds)

- Custom draw (comctl32 v6): `NMCUSTOMDRAW` has **no `dwReturnFlags` field** — the CDRF_* flags are the **return value of the `WM_NOTIFY` handler** (`app.cpp` returns them from `onNotify`/`onCustomDraw`; returning 0 silently disables custom draw). Subitem drawing is a **two-stage handshake**: `CDDS_PREPAINT` → return `CDRF_NOTIFYITEMDRAW`, then `CDDS_ITEMPREPAINT` → return `CDRF_NOTIFYSUBITEMDRAW`, then handle `CDDS_ITEMPREPAINT | CDDS_SUBITEM`. Note `CDRF_NOTIFYITEMDRAW` == `CDRF_NOTIFYSUBITEMDRAW` == 0x20 — the control distinguishes them by the stage returned at, so skipping the `CDDS_ITEMPREPAINT` case silently yields item-level notifications only (this is what kept the Free % bar from drawing). Row = `nmcd.dwItemSpec`. An earlier `src/customdraw.h` (a "classic" `RealNMLVCUSTOMDRAW` + `CDRF_PREPAINT*` constants 1..6) was a misdiagnosis and is deleted. Full write-up: `docs/Common Solutions.md`.
- `ListView_SetItemText` macro breaks string literals under `/Zc:strictStrings` (assigns to non-const `LV_ITEM.pszText`). Use the `lvInsertItem`/`lvSetItemText` helpers in `src/app.cpp`.
- `ListView_InsertItem`/`ListView_SetItem` are 1-arg macros taking `const LV_ITEM*` (no 3-arg form).
- `ListBox_*` convenience macros are missing from commctrl.h; use `LB_*` messages via `SendMessageW` (helpers in `src/editdialog.cpp`).
- GDI+ headers have gaps: `PROPID` (only in `infotech.h`), `MIDL_INTERFACE` (missing from COM headers), and `Gdiplus::byte` (missing from `gdiplusbase.h`). `src/gdiplus_prelude.h` shims all three — include it, never `<gdiplus.h>` directly.
- Dialog fonts: hand-built `CreateFontW(..., L"Segoe UI")` fonts render heavier than expected on this machine, and a control falls back to the default GUI font if its `HFONT` is deleted while in use (controls store the handle, they don't reference the GDI object). Dialogs use `lfMessageFont` (FW_NORMAL) and keep the font alive until `WM_DESTROY`. Full write-up: `docs/Common Solutions.md`.
- `windowsx.h` (in `shared\`) is reduced: it has the `WM_DRAWITEM`/`WM_MEASUREITEM` message macros but **not the owner-draw item-action constants** `DA_DRAWN`/`DA_REDO`/`DA_SKIP`. `src/editdialog.cpp` defines them locally (guarded `#ifndef`) for the owner-drawn list box.

## Config

- Share list: `%APPDATA%\RDSM\shares.json` (i.e. `C:\Users\brad\AppData\Roaming\RDSM\shares.json`). App creates dir/file on first run, seeding the 6 shares from `RDSM.ps1`.
- `--JSON <FileName>`: use `%APPDATA%\RDSM\<FileName>` instead of `shares.json`. A missing/invalid custom file starts with an **empty** share list (no seeding) and the file is created.
- JSON shape: `{"shares": [...], "columnWidths": [...], "windowSize": {"width","height"}, "windowPosition": {"x","y"}, "autoRefreshMinutes": 15}`. The UI-state fields are optional (omitted until first saved); `autoRefreshMinutes` is always written. Top-level array also accepted on read (shares only).
- UI state (column widths, window size, position) is saved on column resize / window resize / window move and restored on startup.
- `autoRefreshMinutes` (1–3600, default 15) is the minutes between automatic checks. Edited via File → Settings...; saved on change. The app auto-refreshes on that interval; the timer is suspended while minimized and restarted (with an immediate refresh) on restore/maximize from minimized.
- **Corrupt file (v1.0):** if the JSON **structure** is unparseable, the file is not read. The app starts with the "new data" (the 6 default shares for the default file, empty for a custom file), **warns the user, and asks permission to overwrite the file** (`App::promptToRewriteCorruptConfig`). Declining leaves the file unchanged and keeps the data in memory only. `loadConfig(LoadStatus*)` reports `Corrupt` so the GUI can prompt. A present but out-of-range `autoRefreshMinutes` falls back to **30** (absent still defaults to 15).

## Architecture (src/)

- `main.cpp` — `wWinMain`: parses `--JSON <FileName>`, common controls + GDI+ startup, runs `App`.
- `app.cpp` — main window: ListView (5 columns, report mode, user-resizable headers), menu, status bar, background worker thread (posts `WM_APP_*`), GDI+ custom draw of a free-space bar in the "Free %" column. Holds a `rdsmon::Config`; subclasses the ListView to catch `HDN_ENDTRACK` (column resize) and saves UI state (column widths / window size / position) to the JSON. Runs an auto-refresh `SetTimer` (suspended while minimized, restarted with an immediate refresh on restore/maximize from minimized).
- `shares.cpp` — `Config` struct (shares + columnWidths + windowSize + windowPosition + autoRefreshMinutes), config load/save (`loadConfig(LoadStatus*)`/`saveConfig`, nlohmann/json vendored at `third_party/nlohmann/json.hpp`), `setConfigFileName` for `--JSON`, `GetDiskFreeSpaceExW` query, byte/percent formatting, and `validateShares` — an **extensible error-test pipeline** (`kShareChecks` table of `ShareCheck` functions; currently *malformed share* — drive letter `C:`–`Z:` or UNC `\\server\share` with valid name chars — and *duplicate share*). A line reports the first error assigned to it.
- `editdialog.cpp` — modal "Edit Shares" dialog (File menu) to add/update/remove/reorder (Move Up/Move Down) entries; uses the system default GUI font; Save rewrites the JSON and the main window's list is refreshed in the new order. Runs `rdsmon::validateShares` on creation and after every change, storing a per-line error. The list box is **owner-drawn** (`LBS_OWNERDRAWFIXED`) so any line with an error is drawn in **red**; a **red static label** in the lower-left corner shows the selected line's error (blank when valid). **Save** is grayed out while the current list has any error. **Cancel and the title-bar X** return to the previously saved configuration, so they are enabled only when the **initially loaded (saved) list passed validation** — if the saved list itself has an error (e.g. hand-edited JSON) they are grayed out, since closing would return to bad data. Move Up/Move Down are **grayed out** when 0–1 shares exist, when the top share is selected (Move Up), or when the bottom share is selected (Move Down).
- `settingsdialog.cpp` — modal "Settings" dialog (File menu) with the Auto Refresh (minutes) field (1–3600); Save rewrites the JSON and restarts the timer.
- `aboutdialog.cpp` — modal "About" dialog (Help menu): GDI+ `WM_PAINT` of `RDSM_128.png` (embedded `RCDATA`, loaded via `CreateStreamOnHGlobal` + `Bitmap::FromStream`) at top-center, plus name, version, and build time from `build_info.h`.
- `resources/resources.rc` — embeds the manifest, `RDSM.ico` (`IDI_APP`), and `RDSM_128.png` (`IDR_ABOUT_PNG`); IDs shared via `resources/resources.h`.

## Current state (v1.0)

Builds clean (no errors, no warnings) and runs. Verified: first run creates `%APPDATA%\RDSM\shares.json` seeded with the 6 shares; subsequent runs load it; window title "Remote Disk Space Monitor"; clean shutdown; icon + About image embedded in the exe; `--JSON <FileName>` creates/uses `%APPDATA%\RDSM\<FileName>` (missing custom file starts empty); config persists `autoRefreshMinutes` (always written, default 15). Exe: `build\bin\Release\RDSM.exe`.

**v1.0 — config error detection and correction:** a corrupt (unparseable) config file is not read; the app starts cleanly (default shares for the default file, empty for a custom file), warns the user, and asks permission to overwrite the file (declining keeps the data in memory only). `autoRefreshMinutes` out of range (1–3600) falls back to 30. The Edit Shares dialog runs an extensible error test (`rdsmon::validateShares`: malformed + duplicate shares), shows the selected line's error in a red lower-left label; Save is grayed out while the current list has an error, and Cancel/X are grayed out when the initially loaded (saved) list itself has an error (closing would return to bad data). Unit-tested in `tests/test_validate.cpp` + `tests/test_config.cpp`; the GUI prompt/dialog are verified by running the exe.

GDI+ header gaps were shimmed in `src/gdiplus_prelude.h`: `typedef DWORD PROPID;`, `#define MIDL_INTERFACE(x) interface __declspec(novtable, uuid(x))`, and `namespace Gdiplus { typedef unsigned char byte; }`. The manifest is embedded via `resources.rc`, so the linker uses `/MANIFEST:NO` (see CMakeLists) to avoid a duplicate MANIFEST resource.

Visually verified: dialog fonts in Settings and Edit Shares render at normal weight (see `docs/Common Solutions.md`); the About dialog (image + name + version + build time + short description); the GDI+ free-space bar in the Free % column (two-stage custom-draw handshake — see SDK gotchas and `docs/Common Solutions.md`); the `Offline` text in the Total Size column; the Edit Shares dialog (add/update/remove + Move Up/Move Down reorder, main window list refreshed in the new order after Save; any line with an error — malformed or duplicate — drawn in red, the selected line's error shown in a red lower-left label, Save grayed out while the current list has an error, and Cancel/X grayed out when the saved list itself has an error); Move Up/Move Down graying at top/bottom and with 0–1 shares); the window/taskbar icon; live share data; the Settings dialog (Auto Refresh field + validation); the auto-refresh behavior (re-queries on the interval, suspends the timer while minimized, immediate refresh + timer restart on restore/maximize from minimized); and the UI-state persistence round-trip (resized column widths / window size / position restored on next launch).
