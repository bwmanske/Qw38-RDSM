# RDSM — Remote Disk Space Monitor (Spec & Session Context)

**Version: 1.0** (single source of truth: `RDSM_VERSION` in `CMakeLists.txt`; surfaced in
the About dialog and this doc).

## Overview

RDSM is a Win32 desktop GUI application (C++17, CMake, MSVC, GDI+) that displays free
disk space for a user-configurable list of UNC network shares and Windows drive
letters. It is a port of
`RDSM.ps1`, a PowerShell script that P/Invokes `GetDiskFreeSpaceEx` and prints
total/used/free/percent for each share.

Project root: `C:\Users\brad\Documents\OpenCode\Qwen3.8-RDSM` (not a git repo).

## Requirements (from user)

1. Read disk space the same way as `RDSM.ps1` (`GetDiskFreeSpaceEx` on UNC paths and drive letters).
2. Tech stack: C++17, CMake, Win32, GDI+.
3. Main window: a ListView with a header and **user-adjustable column widths**.
4. Columns, left to right: `Share Name`, `Total Size`, `Used Space`, `Free Space`, `Free %`.
5. Share list is read from a JSON file in the RDSM config folder under Roaming AppData.
   - **Confirmed with user:** the folder is `C:\Users\brad\AppData\Roaming\RDSM`
     (i.e. `%APPDATA%\RDSM`). The user originally wrote `%AppData%\Roaming\RDSM`, but
     `%AppData%` already includes `Roaming`; the doubled path was explicitly rejected.
6. The app must create the config directory if it does not exist.
7. There must be a method of modifying the share list (implemented as an in-app dialog).
8. **User directive:** use nlohmann/json (from GitHub) for JSON, vendored via a one-time
   download (not FetchContent). Done: v3.12.0 single header at `third_party/nlohmann/json.hpp`.
9. Command line option `--JSON <FileName>`: when supplied, load and save `<FileName>` in
   place of the default `shares.json`. The file is placed in the default config directory
   (`%APPDATA%\RDSM`). If the supplied file fails to load (missing or invalid), start with
   **no data** (no seeding of the 6 defaults) and initialize the file.
10. Persist UI state in the JSON: each column width (saved after the user resizes a
    column), the window size (saved after the user resizes the window), and the window
    position (saved after the user moves the window). All are read back on startup.
11. A **Settings** dialog (added to the menu) with a single setting: **Auto Refresh**,
    the number of minutes between automatic checks. Allowed range 1–3600, default 15.
    The value is saved to the JSON when changed.
12. **Auto refresh:** the app re-queries the shares automatically every Auto Refresh
    minutes. It does **not** refresh while the window is minimized. When the window goes
    from minimized to restored or maximized, it refreshes **immediately**.
13. **Config error detection and correction (v1.0):**
    - A *corrupted* file is one whose JSON **structure** is valid but whose **content** is
      bad (malformed or duplicate shares, or other data a user mistyped in an editor).
    - If the JSON **structure** is corrupt (not parseable), the file is **not read**. The
      app must still start cleanly with no data read. It then **warns the user and asks
      permission to write** the file. If the user declines, the file is left unchanged and
      the in-memory data is kept only until exit.
    - Only starting with **no `--JSON` on the command line** loads the default shares
      (a corrupt/missing default file seeds the 6 defaults; a corrupt/missing custom file
      starts empty).
    - On startup the `autoRefreshMinutes` read from the JSON is validated to 1–3600; an
      out-of-range value is replaced by the default of **30**.
    - The **Edit Shares** dialog runs an extensible **error test** over the list (a
      pipeline of checks; currently *malformed share* and *duplicate share*). A share is
      valid if it is a drive letter `C:`–`Z:` or a UNC path `\\server\share` with valid
      name characters. The test remembers the error for **each** listbox line. A **red
      static label** in the lower-left corner shows the error for the selected line (blank
      when that line is valid). The test runs on dialog creation and after every change.
      **Save** is grayed out while the current list has any error. **Cancel and the
      title-bar X** return to the previously saved configuration, so they are enabled only
      when the **initially loaded (saved) list passed validation** — if the saved list
      itself has an error (e.g. the user hand-edited the JSON), they are grayed out, since
      closing would return to bad data.

## Design decisions

- **Config file:** `%APPDATA%\RDSM\shares.json` by default, or `%APPDATA%\RDSM\<FileName>`
  when `--JSON <FileName>` is supplied. Shape:
   ```json
   {
     "shares": ["\\\\server\\share", ...],
     "columnWidths": [280, 110, 110, 110, 130],
     "windowSize": {"width": 940, "height": 540},
     "windowPosition": {"x": 100, "y": 100},
     "autoRefreshMinutes": 15
   }
   ```
    `columnWidths`, `windowSize`, and `windowPosition` are optional (omitted when not yet
    saved). `autoRefreshMinutes` is always written (default 15 when the field is absent; a
    present but out-of-range value falls back to 30). A bare top-level JSON array is also
    accepted on read (shares only).
   Written with `nlohmann::json::dump(2)`.
- **First run (default file):** if `shares.json` doesn't exist, it is created and seeded
  with the 6 shares from `RDSM.ps1`: `\\Chip\work`, `\\Dale\work`, `\\bmanske-plex-pc\Plex-I`,
  `\\bmanske-plex-pc\Plex-J`, `\\bmanske-plex-pc\Plex-K`, `\\bmanske-game-pc\Game-D`.
  If the file exists but is invalid JSON, it is NOT clobbered (empty config returned).
- **First run (custom `--JSON` file):** if the file doesn't exist, it is created with an
  **empty** share list (no seeding). If it exists but is invalid JSON, an empty config is
  returned and the file is NOT clobbered.
- **`--JSON <FileName>` parsing:** `wWinMain` parses the command line with
  `CommandLineToArgvW` and looks for `--JSON` (case-insensitive) followed by a file name.
  The name is passed to `rdsmon::setConfigFileName()` before the app runs; the file is
  always placed in the default config directory.
- **UI state persistence:** the `App` holds a `rdsmon::Config` (shares + column widths +
  window size + position). On startup (`App::run`) the config is loaded before the window
  is created, so the saved size/position are applied at `CreateWindowExW` and the saved
  column widths are applied in `createListView`. Saves happen on:
  - **Column resize:** the ListView is subclassed to catch `HDN_ENDTRACK` (sent by the
    header control when a column-divider drag ends); it posts `WM_APP_COLUMN_RESIZED`,
    which reads the current widths via `ListView_GetColumn` and saves the config.
  - **Window resize/move:** `WM_EXITSIZEMOVE` (sent when the user finishes resizing or
    moving the window) saves both the size and position via `GetWindowRect`.
- **Background worker thread:** share queries run off the UI thread (network shares can
  hang for seconds). The worker copies the share list, queries each share, and posts
  `WM_APP_CLEAR` / `WM_APP_SHARE_RESULT` (wParam = row index) / `WM_APP_DONE` to the main
  window. Results are stored in `App::results_` guarded by `App::resultsMutex_`.
  `App::stopWorker()` joins the thread (called before the edit dialog and on destroy).
- **GDI+ usage:** custom-draw (`NM_CUSTOMDRAW`) renders a free-space bar in the `Free %`
  column: light-gray track, filled portion = free %, color green (>50%), amber (>20%),
  red (≤20%), percentage text centered on top. GDI+ is started/stopped in `wWinMain`.
- **Offline shares:** row shows `Offline` in the `Total Size` column (the column just
  right of the share name); the other size columns are left blank. The Win32 error string
  (via `FormatMessageW`) appears in the status bar when the row is selected.
- **Status bar:** shows `N of M shares online`; replaced by the error text when an offline
  row is selected.
- **Edit Shares dialog** (File menu): modal, created programmatically (registered window
  class + local message loop with `IsDialogMessageW`, parent disabled). Controls: ListBox
  of shares, Edit box, buttons Add / Update / Remove / Move Up / Move Down / Save / Cancel.
  Move Up / Move Down reorder the selected share within the list. All controls use the
  system default GUI font (via `SystemParametersInfoW(SPI_GETNONCLIENTMETRICS)` →
  `lfMessageFont`) so the list box renders with the normal, non-bold system font. Save
  returns the new (possibly reordered) list to `App`, which calls `saveConfig()` and
  `refresh()` (rebuilding the main window's list in the new order).
  - **Duplicate detection:** the list box is owner-drawn (`LBS_OWNERDRAWFIXED`) so the
    **first instance of a duplicated share name** (case-insensitive) is drawn in **red**.
     Duplicates are checked on open and re-checked after every Add / Update / Remove /
     Move. While any red entry exists the dialog **cannot be closed** — Save, Cancel, and
     the title-bar X are all **grayed out** (disabled) — but Update and Remove remain
     enabled so the user can clear the problem.
  - **Move Up / Move Down graying:** both are disabled when 0 or 1 shares exist; Move Up
    is additionally disabled when the top share is selected, and Move Down when the bottom
    share is selected.
- **Settings dialog** (File menu): modal, created programmatically like the Edit dialog
  (registered window class + local message loop, parent disabled). Fixed-size, non-
  resizable. A single setting: **Auto Refresh (minutes)** — an `ES_NUMBER` edit box
  pre-populated with the current value. Save validates the input is a whole number in
  1–3600 (showing a warning `MessageBox` and staying open otherwise); on success it returns
  the value to `App`, which updates `config_.autoRefreshMinutes`, calls `saveConfig()`, and
  restarts the auto-refresh timer.
- **Auto refresh:** a Win32 timer (`SetTimer`, ID `IDT_AutoRefresh`) fires every
  `autoRefreshMinutes` minutes (interval in ms = minutes × 60 × 1000; the max of 3600
  minutes = 216,000,000 ms fits the `UINT` `uElapse`). On `WM_TIMER`, the app refreshes.
  The timer is **suspended while the window is minimized**: minimize/restore transitions
  are tracked via `WM_SIZE`'s `wParam` — `SIZE_MINIMIZED` sets a `wasMinimized_` flag and
  kills the timer; `SIZE_RESTORED` or `SIZE_MAXIMIZED` (if the window was minimized)
  triggers an **immediate** refresh, restarts the timer, and clears the flag. The timer is
  also (re)started on create and whenever the interval changes, and killed on destroy.
- **Menu:** File → Refresh (F5), Edit Shares..., Settings..., Exit; Help → About.
- **About dialog** (Help → About): modal, created programmatically like the Edit dialog
  (registered window class + local message loop, parent disabled). Fixed-size, non-
  resizable. `WM_PAINT` draws, via GDI+: the app image `RDSM_128.png` (embedded as an
  `RCDATA` resource, loaded through `CreateStreamOnHGlobal` + `Bitmap::FromStream`) at the
  top center, then the app name, `Version <RDSM_VERSION>`, and `Built: <timestamp>`. A
  single OK button closes it.
- **Version + build timestamp:** `RDSM_VERSION` is set in `CMakeLists.txt`. A CMake
  custom target (`BuildInfo`, runs on every build) executes `cmake/BuildInfo.cmake`, which
  writes `build/generated/build_info.h` defining `RDSM_VERSION` and `RDSM_BUILD_TIMESTAMP`.
  The timestamp is the build time in a 12-hour clock to the second (e.g.
  `2026-10-06 09:03:20 AM`). CMake's `string(TIMESTAMP)` lacks the `%p` (AM/PM) specifier,
  so the 24-hour value is parsed by fixed position and converted in the script.
- **App icon:** `RDSM.ico` (multi-size) is embedded via `resources.rc` (`IDI_APP`) and set
  as the window-class large/small icon.
- **Sizing:** columns are resizable by dragging header dividers (default report-view
  behavior). Initial widths: 280/110/110/110/130.
- **Formatting:** `formatBytes` picks B/KB/MB/GB/TB/PB (2 decimals above bytes);
  `formatPercent` is 1 decimal + `%`.

## UI mockups

ASCII mockups of the main window and the three dialogs, drawn to match the control
layout in `src/`. They are schematic (not to pixel scale); the notes under each
capture the behavior the drawing cannot show.

### Main window

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│  Remote Disk Space Monitor                                                               │
├──────────────────────────────────────────────────────────────────────────────────────────┤
│  File   Help                                                                             │
├──────────────────────────────────────────────────────────────────────────────────────────┤
│  Share Name                    │ Total Size │ Used Space │ Free Space │ Free %           │
├────────────────────────────────┼────────────┼────────────┼────────────┼──────────────────┤
│  \\Chip\work                   │  2.00 TB   │  1.20 TB   │  800.00 GB │ ▓▓▓▓▓▓░░░░ 40.0% │
│  \\Dale\work                   │  1.00 TB   │  900.00 GB │  100.00 GB │ ▓░░░░░░░░░ 10.0% │
│  \\bmanske-plex-pc\Plex-I      │  Offline   │            │            │                  │
│  \\bmanske-game-pc\Game-D      │  500.00 GB │  100.00 GB │  400.00 GB │ ▓▓▓▓▓▓▓▓░░ 80.0% │
└────────────────────────────────┴────────────┴────────────┴────────────┴──────────────────┘
│  3 of 4 shares online                                                                    │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Menu:** `File` → Refresh (F5), Edit Shares..., Settings..., ───, Exit; `Help` → About.
- **Header row:** custom-drawn 10% darker than the default face color so the column titles
  stand out; column dividers are redrawn as thin lines. Columns are user-resizable by
  dragging the dividers (widths persist to the JSON).
- **Columns** (default widths 280/110/110/110/130): `Share Name` (left), `Total Size`,
  `Used Space`, `Free Space` (all right-aligned), `Free %` (left).
- **Free % bar:** GDI+ custom draw — a light-gray track with a filled portion equal to the
  free %, colored **green** (>50%), **amber** (>20%), **red** (≤20%), with the percentage
  text centered on top.
- **Offline rows:** `Offline` is shown in the `Total Size` column (just right of the share
  name); the other size columns are left blank. Selecting the row shows the Win32 error
  string (via `FormatMessageW`) in the status bar.
- **Status bar:** `N of M shares online`; replaced by the error text while an offline row
  is selected.

### Edit Shares dialog (File → Edit Shares...)

```
┌─ Edit Shares ─────────────────────────────────────────────────────────────────────────────┐
│                                                                                           │
│  ┌─────────────────────────────────────────────────────────────────────────────────────┐  │
│  │  \\Chip\work                                                                        │  │
│  │  \\Dale\work                                                                        │  │
│  │  \\bmanske-plex-pc\Plex-I                                                           │  │
│  │  \\bmanske-plex-pc\Plex-J                                                           │  │
│  │  \\bmanske-plex-pc\Plex-K                                                           │  │
│  │  \\bmanske-game-pc\Game-D                                                           │  │
│  └─────────────────────────────────────────────────────────────────────────────────────┘  │
│  ┌─────────────────────────────────────────────────────────────────────────────────────┐  │
│  │  \\Chip\work                                                                        │  │
│  └─────────────────────────────────────────────────────────────────────────────────────┘  │
│  [ Add ]  [ Update ]  [ Remove ]  [ Move Up ]  [ Move Down ]                              │
│                                                                                           │
│                                   [ Save ]  [ Cancel ]                                    │
└───────────────────────────────────────────────────────────────────────────────────────────┘
```

- **List box** (top, full width): the share list, owner-drawn so the **first instance of a
  duplicated share name** (case-insensitive) is drawn in **red**. Selecting an item copies it
  into the edit box below.
- **Edit box** (below the list, full width): the share to Add, or the text to Update the
  selected item with.
- **Button row** (left-aligned): `Add` (append the edit text), `Update` (replace the selected
  item), `Remove` (delete the selected item), `Move Up` / `Move Down` (reorder the selected
  item). Move Up/Move Down gray out when 0–1 shares exist, when the top share is selected
  (Move Up), or when the bottom share is selected (Move Down).
- **Bottom row** (right-aligned): `Save` (rewrite the JSON and refresh the main list in the
  new order) and `Cancel` (discard). While any red (duplicate) entry remains, `Save`,
  `Cancel`, and the title-bar X are all **grayed out** (the dialog cannot be closed); Update
  and Remove stay enabled so the duplicate can be cleared.

### Settings dialog (File → Settings...)

```
┌─ Settings ─────────────────────────────────────────────┐
│                                                        │
│  Auto Refresh (minutes):                               │
│  ┌──────────────┐                                      │
│  │ 15           │                                      │
│  └──────────────┘                                      │
│                                                        │
│                               [ Save ]  [ Cancel ]     │
└────────────────────────────────────────────────────────┘
```

- Fixed-size, non-resizable. A single setting: **Auto Refresh (minutes)** — an `ES_NUMBER`
  edit box pre-populated with the current value (default 15).
- `Save` validates the input is a whole number in **1–3600** (showing a warning `MessageBox`
  and staying open otherwise); on success it saves to the JSON and restarts the auto-refresh
  timer. `Cancel` discards.

### About dialog (Help → About)

```
┌─ About RDSM ───────────────────────────────────────────┐
│                                                        │
│                 ┌────────────────────┐                 │
│                 │                    │                 │
│                 │     RDSM_128.png   │                 │
│                 │      (128×128)     │                 │
│                 │                    │                 │
│                 └────────────────────┘                 │
│                                                        │
│              Remote Disk Space Monitor                 │
│                                                        │
│                      Version 0.9                       │
│                                                        │
│                 Built: 2026-10-06 09:03:20 AM          │
│                                                        │
│    Queries free space on a list of UNC network         │
│    shares and Windows drive letters.                   │
│                                                        │
│                                                        │
│                       [ OK ]                           │
└────────────────────────────────────────────────────────┘
```

- Fixed-size, non-resizable. `WM_PAINT` draws, via GDI+: the app image `RDSM_128.png`
  (embedded `RCDATA`, loaded through `CreateStreamOnHGlobal` + `Bitmap::FromStream`) at the
  top center, then the app name (Segoe UI 13 bold), `Version <RDSM_VERSION>`,
  `Built: <timestamp>`, and a short description — all centered.
- A single `OK` button (default push button) centered at the bottom closes the dialog.

## File layout

```
CMakeLists.txt                 MSVC, C++17, /W4 /permissive- /utf-8, UNICODE defines,
                                 RDSM_VERSION, BuildInfo custom target, links comctl32 gdiplus
                                 gdi32 user32 shell32 ole32 advapi32 kernel32, WIN32 subsystem,
                                 exe -> build/bin
build.ps1                      build helper: stop RDSM.exe, configure, build, test
                                 (wraps the CMake commands; see docs/BUILDING.md)
cmake/BuildInfo.cmake          build-time script: writes build/generated/build_info.h
                                 (RDSM_VERSION + RDSM_BUILD_TIMESTAMP, 12-hour to the second)
RDSM.ps1                       original PowerShell script (reference only)
AGENTS.md                      short agent instructions
specs/RDSM spec.md             this file
docs/
  BUILDING.md                  how to build with build.ps1 and with raw CMake
  TESTING.md                   how to run the CTest unit tests
  USER_GUIDE.md                end-user guide
  Common Solutions.md          write-ups of the font + custom-draw issues
third_party/nlohmann/json.hpp  vendored nlohmann/json v3.12.0 (single header)
src/
  main.cpp                     wWinMain: parse --JSON <FileName> (CommandLineToArgvW),
                                 InitCommonControlsEx (LISTVIEW|BAR), GdiplusStartup,
                                 App::instance()->init()/run(), GdiplusShutdown
   app.h / app.cpp              App singleton: window class (+IDI_APP icon), menu, ListView,
                                  status bar, worker thread, GDI+ custom draw, WM_APP_* handling,
                                  Config member, ListView subclass (HDN_ENDTRACK), saves UI state,
                                  auto-refresh timer (SetTimer/WM_TIMER) + minimize tracking
   shares.h / shares.cpp        namespace rdsmon: Config struct (shares + columnWidths +
                                  windowSize + windowPosition + autoRefreshMinutes),
                                  configDir/configFilePath/setConfigFileName/
                                  hasCustomConfigFile/ensureConfigDir, loadConfig/saveConfig
                                  (nlohmann), queryShare (GetDiskFreeSpaceExW, trailing
                                  backslash appended), formatBytes/formatPercent, lastErrorString
    editdialog.h / editdialog.cpp  EditDialog::show(parent, initial, outShares) -> bool:
                                   add/update/remove + move up/down, system GUI font
   settingsdialog.h / settingsdialog.cpp  SettingsDialog::show(parent, current, out) -> bool:
                                  Auto Refresh (minutes) field, 1-3600 validation
   aboutdialog.h / aboutdialog.cpp  AboutDialog::show(parent): GDI+ WM_PAINT of RDSM_128.png
                                 (from RCDATA via IStream) + name/version/build time + OK
   gdiplus_prelude.h            shims for this SDK's GDI+ gaps (PROPID, MIDL_INTERFACE,
                                Gdiplus::byte) then #include <gdiplus.h>
  resources/app.manifest       Common Controls v6 dependency, system DPI awareness,
                                supportedOS GUIDs
  resources/resources.h        shared resource IDs (IDI_APP, IDR_ABOUT_PNG)
  resources/resources.rc       embeds manifest (1 24), icon (IDI_APP ICON RDSM.ico),
                                About image (IDR_ABOUT_PNG RCDATA RDSM_128.png)
  resources/RDSM.ico           multi-size application icon
  resources/RDSM_128.png       About dialog image (128x128)
```

## Build

Full instructions (prerequisites, `build.ps1` options, and the equivalent manual
CMake commands) are in `docs/BUILDING.md`.

Quick start — run the helper script from the project root:

```
.\build.ps1
```

It stops any running `RDSM.exe`, configures (if needed), builds **Release**, and
runs the unit tests. The raw CMake commands it wraps are:

```
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- CMake 4.3.3 auto-selects generator "Visual Studio 18 2026", MSVC 19.51.36248
  (toolset 14.51.36231), Windows SDK 10.0.26100.0. No env activation needed.
- Output: `build\bin\Release\RDSM.exe`.
- Each build runs the `BuildInfo` custom target, regenerating
  `build\generated\build_info.h` (version + build timestamp) before compiling.
- Unit tests (CTest) cover the non-GUI logic in `src/shares.cpp`; the GUI is
  verified by running the exe. See `docs/TESTING.md`. No linter.

## Environment gotchas

- **curl is broken on this machine** (schannel `SEC_E_INVALID_TOKEN`, exit 35) and
  `Invoke-WebRequest` also fails ("Cannot determine the frame size"). Use `gh api`
  (gh 2.96.0, logged in as `bwmanske`) for GitHub downloads, e.g.:
  `gh api "repos/<owner>/<repo>/contents/<path>?ref=<tag>" -H "Accept: application/vnd.github.raw+json" > out`
- Shell is PowerShell 7 (pwsh) on win32.

## SDK gotchas (standard Windows SDK 10.0.26100 behaviors that need workarounds)

All of these were hit and worked around. They are standard SDK behaviors, not a
broken SDK — an earlier note claiming the SDK was "non-standard" (and the
`src/customdraw.h` `RealNMLVCUSTOMDRAW` workaround it spawned) was a misdiagnosis
that broke the Free % bar and has been removed.

1. **Custom draw (comctl32 v6) return flags are the `WM_NOTIFY` return value, and
   subitem drawing is a two-stage handshake.**
   `commctrl.h` declares `NMCUSTOMDRAW` as
   `tagNMCUSTOMDRAWINFO { NMHDR hdr; DWORD dwDrawStage; HDC hdc; RECT rc; DWORD_PTR
   dwItemSpec; UINT uItemState; LPARAM lItemlParam; }` — this matches the Microsoft
   docs exactly and has **no `dwReturnFlags` field**. The CDRF_* flags the control
   acts on are returned from the `WM_NOTIFY` handler, so `app.cpp` returns them from
   `onNotify`/`onCustomDraw` (returning 0 silently disables custom draw). To get
   per-subitem notifications the handler must return `CDRF_NOTIFYITEMDRAW` at
   `CDDS_PREPAINT` **and** `CDRF_NOTIFYSUBITEMDRAW` at `CDDS_ITEMPREPAINT`, then
   handle `CDDS_ITEMPREPAINT | CDDS_SUBITEM`. Note `CDRF_NOTIFYITEMDRAW` ==
   `CDRF_NOTIFYSUBITEMDRAW` == 0x20 — the control distinguishes them by the stage
   returned at, so skipping the `CDDS_ITEMPREPAINT` case silently yields item-level
   notifications only (this is what kept the Free % bar from drawing). Row index =
   `nmcd.dwItemSpec`; subitem = `iSubItem`.
2. **Draw-stage constants are `CDDS_*`, return flags are `CDRF_*`** — both are
   defined in commctrl.h (`CDDS_PREPAINT`, `CDDS_ITEMPREPAINT`, `CDDS_SUBITEM`,
   `CDRF_DODEFAULT`, `CDRF_SKIPDEFAULT`, `CDRF_NOTIFYSUBITEMDRAW`, ...). Do not
   invent `CDRF_PREPAINT*`-style stage constants.
3. **`ListView_SetItemText` macro** assigns its text arg to `LV_ITEM.pszText`, which is
   non-const `LPWSTR` → string literals fail under `/Zc:strictStrings`
   (C2440 "loses const qualifier"). Workaround: `lvInsertItem`/`lvSetItemText` helpers
   in `app.cpp` build an `LVITEMW` and use `const_cast<LPWSTR>(text)` (safe: the control
   only reads it).
4. **`ListView_InsertItem` / `ListView_SetItem` are 1-arg macros** taking
   `(hwnd, const LV_ITEM*)` — there is no 3-arg `(hwnd, i, text)` form.
5. **`ListBox_*` convenience macros are missing** (`ListBox_GetCurSel`,
   `ListBox_SetCurSel`, `ListBox_AddString`, `ListBox_ResetContent`). Use the `LB_*`
   messages via `SendMessageW` (helpers `lbGetSel/lbSetCurSel/lbReset/lbAddString` in
   `editdialog.cpp`). The `LB_*` message constants exist in winuser.h.
 6. **GDI+ `PROPID`** is only defined in `infotech.h` (which includes comdef.h/ocidl.h —
    heavy). `src/gdiplus_prelude.h` does `typedef DWORD PROPID;` (C++11 allows identical
    typedef redefinition) before `#include <gdiplus.h>`. Include the prelude, never
    `<gdiplus.h>` directly.
 7. **GDI+ `MIDL_INTERFACE` is missing** from the usual COM headers (it only appears in
    unrelated headers like `MqOaI.h`/`scesvc.h`). `GdiplusImaging.h` uses it to declare
    `IImageBytes : public IUnknown`. The prelude defines
    `#define MIDL_INTERFACE(x) interface __declspec(novtable, uuid(x))` and includes
    `<unknwn.h>` for `IUnknown`.
 8. **GDI+ `Gdiplus::byte` is missing** from this SDK's `gdiplusbase.h`, but
    `GdiplusPath.h` (and others) use it. The prelude declares
    `namespace Gdiplus { typedef unsigned char byte; }` before including `<gdiplus.h>`.
  9. **Duplicate MANIFEST resource (CVT1100).** The MSVC linker auto-generates AND embeds
     a manifest by default; embedding one via `resources.rc` too causes a duplicate. Fix:
     `target_link_options(RDSM PRIVATE /MANIFEST:NO)` in CMakeLists so only the `.rc`
     manifest is used.
  10. **`windowsx.h` is reduced — the owner-draw item-action constants are missing.**
      This SDK's `windowsx.h` (in `shared\`) defines the `WM_DRAWITEM`/`WM_MEASUREITEM`
      message macros but **not** `DA_DRAWN`/`DA_REDO`/`DA_SKIP`. `editdialog.cpp` defines
      them locally (guarded `#ifndef`: `DA_DRAWN 1`, `DA_REDO 2`, `DA_SKIP 3`) for the
      owner-drawn list box.

## Current state (v1.0)

Builds clean (no errors, no warnings) and runs. Verified by launching the exe:
- first run creates `C:\Users\brad\AppData\Roaming\RDSM\shares.json` seeded with the 6 shares;
- subsequent runs load the existing file (share count preserved, not re-seeded);
- window title "Remote Disk Space Monitor"; clean shutdown;
- `build/generated/build_info.h` is regenerated on every build with the version and a
  12-hour build timestamp to the second;
- the icon and About image are embedded in the exe (`.rsrc` section size matches the sum
  of `RDSM.ico` + `RDSM_128.png` + manifest);
- `--JSON <FileName>` uses `%APPDATA%\RDSM\<FileName>` in place of `shares.json`; a missing
  custom file starts with an empty share list and is created (verified: launching with
  `--JSON testfile.json` created the file with `{"shares": []}` and the app stayed alive);
- the config now also persists column widths, window size, and window position (written on
  column resize / window resize / window move, read back on startup);
- the config now also persists `autoRefreshMinutes` (always written; verified: a fresh
  custom file is created with `"autoRefreshMinutes": 15`);
- a **Settings** dialog (File → Settings...) edits the Auto Refresh interval (1–3600,
  default 15) and saves it to the JSON; an auto-refresh timer re-queries every N minutes,
  skips while minimized, and refreshes immediately on restore/maximize from minimized.

**v1.0 — config error detection and correction** (unit-tested in `tests/test_validate.cpp`
and `tests/test_config.cpp`; the GUI prompt/dialog are verified by running the exe):
- A corrupt (unparseable) config file is **not read**; the app starts cleanly with the
  "new data" (the 6 default shares for the default file, an empty list for a custom file),
  **warns the user, and asks permission to overwrite the file**. Declining leaves the file
  unchanged and keeps the in-memory data only until exit (see `App::promptToRewriteCorruptConfig`).
- `autoRefreshMinutes` read from the JSON is validated to 1–3600; a present but out-of-range
  value falls back to **30** (an absent field still defaults to 15).
- The **Edit Shares** dialog runs an extensible **error test** (`rdsmon::validateShares`, a
  pipeline of checks in `shares.cpp` — currently *malformed share* and *duplicate share*).
  A share is valid if it is a drive letter `C:`–`Z:` or a UNC path `\\server\share` with
  valid name characters. The test stores the error for **each** listbox line; a **red static
  label** in the lower-left corner shows the selected line's error (blank when valid). The
  test runs on dialog creation and after every Add/Update/Remove/Move. **Save** is grayed
  out while the current list has any error. **Cancel and the title-bar X** are enabled only
  when the initially loaded (saved) list passed validation — if the saved list itself has an
  error (e.g. hand-edited JSON), they are grayed out since closing would return to bad data.

Exe: `build\bin\Release\RDSM.exe` (note the `Release` subdir — the VS multi-config
generator appends the config to `RUNTIME_OUTPUT_DIRECTORY`).

**Visually verified** (on the desktop): the GDI+ free-space bar in the Free % column
(two-stage custom-draw handshake — see SDK gotchas and `docs/Common Solutions.md`), the
`Offline` text in the Total Size column for unreachable shares, the Edit Shares dialog
(add/update/remove + move up/down reorder; Save rewrites the JSON and refreshes the main
window's list in the new order; any line with an error — malformed or duplicate — is drawn
in red, the selected line's error is shown in a red lower-left label, Save is grayed out
while the current list has an error, and Cancel/X are grayed out when the saved list itself
has an error); Move Up/Move Down gray out at the top/bottom and with 0–1 shares), the About
dialog (image +
name + version + build time + short description), the window/taskbar icon, live share
data, the Settings dialog (Auto Refresh field + validation), the auto-refresh behavior
(re-queries on the interval, suspends the timer while minimized, immediate refresh + timer
restart on restore/maximize from minimized), and the UI-state persistence round-trip
(resized column widths / window size / position restored on the next launch).

## Open items / possible follow-ups (not requested yet)

- Per-row tooltips for offline errors (currently status bar only).
