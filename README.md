# RDSM — Remote Disk Space Monitor

A native Windows desktop app that shows free space across a list of UNC network
shares and local drive letters at a glance. Built with Win32 + C++17 (no runtime
dependencies beyond the OS), it is a from-scratch port of the original
`RDSM.ps1` PowerShell script.

![status](https://img.shields.io/badge/version-0.9-lightgrey)
![platform](https://img.shields.io/badge/platform-Windows-blue)
![language](https://img.shields.io/badge/language-C%2B%2B17-lightgrey)

## Features

- **Live free-space table** — a five-column report view: `Share Name`, `Total
  Size`, `Used Space`, `Free Space`, and `Free %`.
- **Color-coded free-space bar** — the `Free %` column draws a GDI+ bar: green
  above 50%, amber above 20%, red at or below 20%, with the percentage centered
  on top.
- **Offline detection** — unreachable shares show `Offline`; selecting the row
  reveals the underlying Win32 error in the status bar.
- **Edit Shares** — add, update, remove, and reorder (Move Up / Move Down)
  entries. The first instance of a duplicated share name is drawn in red, and
  the dialog cannot be closed until duplicates are cleared.
- **Auto refresh** — re-queries on a configurable interval (1–3600 minutes,
  default 15). The timer suspends while the window is minimized and fires
  immediately on restore.
- **Remembers your layout** — column widths, window size, and window position
  persist between runs.
- **Per-profile config** — `--JSON <FileName>` loads an alternate share list,
  handy for monitoring different sets of machines.

## Screenshot

The main window lists each share with its total/used/free space and a
color-coded free-space bar. Offline shares are flagged, and the status bar
reports how many shares are online.

## Building

Requires **Visual Studio 2026** (MSVC 19.51) with the C++ desktop workload and
the **Windows 10/11 SDK**, plus **CMake**. CMake auto-detects the toolchain —
no environment activation needed.

The quickest way is the helper script from the project root:

```powershell
.\build.ps1
```

It stops any running `RDSM.exe`, configures (if needed), builds **Release**,
and runs the unit tests. The equivalent manual commands are:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is written to `build\bin\Release\RDSM.exe`.

Full build details, including `build.ps1` options, are in
[`docs/BUILDING.md`](docs/BUILDING.md).

## Using

Run `RDSM.exe`. On first launch it creates `%APPDATA%\RDSM\shares.json` seeded
with a default share list; every change you make is saved back to that file.

- **File → Refresh** (F5) — re-query all shares now.
- **File → Edit Shares…** — manage the share list.
- **File → Settings…** — set the auto-refresh interval.
- **Help → About** — version and build information.

To use a separate share list, launch with `RDSM.exe --JSON <FileName>`; it
reads/writes `%APPDATA%\RDSM\<FileName>` instead of `shares.json`.

See [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) for a full walkthrough.

## Project layout

```
CMakeLists.txt                 build definition (MSVC, C++17, /W4 /permissive-)
build.ps1                      build helper (stop, configure, build, test)
cmake/BuildInfo.cmake          generates build/generated/build_info.h at build time
src/                           application source (window, dialogs, config, queries)
src/resources/                 manifest, icon, About image, resource script
tests/                         dependency-free CTest unit tests
third_party/nlohmann/json.hpp  vendored nlohmann/json (single header)
docs/                          building, testing, user guide, solution write-ups
specs/RDSM spec.md             full design spec + UI mockups
RDSM.ps1                       original PowerShell reference
```

## Documentation

- [`docs/BUILDING.md`](docs/BUILDING.md) — how to build (with `build.ps1` or raw CMake)
- [`docs/TESTING.md`](docs/TESTING.md) — how to run the unit tests
- [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) — end-user guide
- [`docs/Common Solutions.md`](docs/Common%20Solutions.md) — write-ups of the GDI+ / custom-draw issues solved
- [`specs/RDSM spec.md`](specs/RDSM%20spec.md) — design spec, requirements, and UI mockups

## Tests

The test suite (`tests/`) unit-tests the non-GUI logic — byte/percent
formatting, share queries, and config load/save — using a small dependency-free
harness. Config tests redirect `APPDATA` to a temp directory and never touch the
real config; query tests use the local `C:` drive and a missing path (no
network). Run them with:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## License

See the repository for licensing terms.
