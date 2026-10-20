# RDSM — Testing

RDSM uses **CTest** (bundled with CMake) to run its unit tests. The tests cover the
non-GUI logic in `src/shares.cpp`; the window and dialog code needs a desktop and is
verified by running the exe (see "What is not unit tested" below).

## Running the tests

From the project root:

```
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`--output-on-failure` prints the test's stdout/stderr only when a test fails, so a
green run is quiet. Add `-V` (or `--verbose`) to always show the per-test output:

```
ctest --test-dir build -C Release -V
```

To run a single test by name (there is currently one CTest entry, `RDSMTests`):

```
ctest --test-dir build -C Release -R RDSMTests --output-on-failure
```

You can also run the test executable directly for the full per-test listing:

```
build\bin\Release\RDSMTests.exe
```

### Disabling the tests

The tests are built by default. To skip them (e.g. for a faster app-only build):

```
cmake -S . -B build -A x64 -DBUILD_TESTING=OFF
```

## Layout

```
tests/
  test_framework.h   minimal dependency-free test harness (TEST / CHECK / CHECK_EQ)
  main.cpp           test runner: runs every registered test, sets the exit code
  test_format.cpp    unit tests for formatBytes / formatPercent
  test_query.cpp     unit tests for queryShare (GetDiskFreeSpaceEx wrapper)
  test_config.cpp    unit tests for configDir / configFilePath / ensureConfigDir /
                     loadShares / saveShares
```

The CMake target `RDSMTests` compiles the test files together with `src/shares.cpp`
(the code under test) and is registered with CTest via `add_test(NAME RDSMTests ...)`.
It is built with the same strict flags as the app (`/W4 /permissive- /utf-8`).

## The test harness

`tests/test_framework.h` is a small, self-contained harness — no external framework,
no downloads (matching how `nlohmann/json` is vendored). Define a test and assert with:

```cpp
#include "test_framework.h"
#include "shares.h"

TEST(my_test_name) {
    CHECK(rdsmon::formatBytes(0) == L"0 B");
    CHECK_EQ(rdsmon::formatPercent(50.0), std::wstring(L"50.0%"));
}
```

- `TEST(name)` registers a test function at static-init time.
- `CHECK(cond)` records a failure (with file:line) if `cond` is false; the test
  continues so one test can surface every broken assertion.
- `CHECK_EQ(a, b)` is `CHECK(a == b)` with both operands named in the failure message.

`main.cpp` runs all registered tests, prints a `[ RUN ]` / `[ OK ]` / `[ FAIL ]` line
per test, and returns **0** if every test passed or **1** otherwise. CTest uses that
exit code to mark the test passed or failed.

## What is unit tested

| Area | Function(s) | Notes |
| --- | --- | --- |
| Byte formatting | `formatBytes` | B/KB/MB/GB/TB/PB boundaries, `%.2f` rendering, and rounding just below a unit boundary |
| Percent formatting | `formatPercent` | one-decimal rendering |
| Share query | `queryShare` | success on a local drive (`C:`), trailing-backslash input, fast failure on a missing path, name preserved |
| Config path | `configDir`, `configFilePath`, `setConfigFileName` | derived from `%APPDATA%`, the `--JSON` custom-name path, and the bare-`RDSM` fallback when `APPDATA` is empty |
| Config dir | `ensureConfigDir` | creates the `RDSM` directory |
| Config I/O | `saveConfig`, `loadConfig` | shares round-trip (incl. Unicode), first-run seeding of the 6 defaults, bare top-level array, invalid JSON (empty + not clobbered), non-string elements skipped, object without a `shares` key, empty list |
| UI state | `saveConfig`, `loadConfig` | column widths / window size / window position round-trip, and reported absent when not saved |
| Auto refresh | `saveConfig`, `loadConfig` | `autoRefreshMinutes` round-trip; default (15) when absent; out-of-range (0, 3601, negative) keeps default; boundary values 1 and 3600 accepted |
| Custom file | `loadConfig` | missing custom file starts empty + is created; invalid custom file starts empty + not clobbered |

### Test isolation for the config tests

`loadConfig`/`saveConfig` read and write `%APPDATA%\RDSM\shares.json` (or a custom file
when `setConfigFileName` is called). The config tests redirect `APPDATA` to a fresh
per-test temp directory (via `SetEnvironmentVariableW`) and reset the custom file name to
the default, so they **never touch the user's real config file**. The temp `APPDATA`
directory is created first because `ensureConfigDir` uses `CreateDirectoryW`, which only
creates the final path component (the real `%APPDATA%` always exists, so this mirrors
production).

### The `queryShare` tests avoid the network

The success case queries the local `C:` drive (present on every Windows machine). The
failure case queries a non-existent subdirectory of `C:`, which fails immediately with
`ERROR_PATH_NOT_FOUND` — so the tests do not depend on any network share being
reachable and do not hang on a network timeout.

## What is not unit tested

The GUI code — `src/app.cpp` (main window, ListView, GDI+ custom draw, worker thread),
`src/editdialog.cpp`, and `src/aboutdialog.cpp` — is not unit tested. It drives real
Win32 windows and GDI+ drawing, which need an interactive desktop. Verify it by building
and running `build\bin\Release\RDSM.exe`:

- the free-space bar renders in the **Free %** column,
- offline rows show **Offline** in the **Total Size** column,
- **File → Edit Shares…** adds/updates/removes/reorders (Move Up/Move Down) entries and rewrites `shares.json`; a duplicated share shows its first instance in **red** and Save/Cancel/X are **grayed out** until the duplicate is fixed; Move Up/Move Down gray out at the top/bottom and with 0–1 shares,
- **Help → About** shows the image, version, and build time,
- **File → Refresh** (F5) re-queries without crashing.

## Adding a test

1. Add a `TEST(...)` to the relevant `tests/test_*.cpp` (or a new file).
2. If you add a new file, list it in the `RDSMTests` `add_executable(...)` in
   `CMakeLists.txt` and reconfigure (`cmake -S . -B build -A x64`).
3. Rebuild and run `ctest --test-dir build -C Release --output-on-failure`.

Keep tests deterministic and fast: no network, no dependence on the user's real config,
and no GUI.
