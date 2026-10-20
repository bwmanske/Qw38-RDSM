# RDSM — Building

This page explains how to build RDSM, both with the `build.ps1` helper script and
with the raw CMake commands it wraps.

## Prerequisites

- **Windows** with **Visual Studio 18 Community** (MSVC 19.51) and the
  **Windows SDK 10.0.26100** installed (the "Desktop development with C++"
  workload). CMake auto-detects the toolchain — no environment activation
  (`vcvarsall.bat`) is needed.
- **CMake** (3.16 or newer; 4.3.3 was used to develop this project) on `PATH`.
  CMake also provides **CTest**, which the unit tests use.
- **PowerShell 7** (`pwsh`) if you use `build.ps1`.

No third-party packages are fetched at build time: `nlohmann/json` is vendored at
`third_party/nlohmann/json.hpp`.

## Building with build.ps1 (recommended)

From the project root, run:

```
.\build.ps1
```

This stops any running `RDSM.exe`, configures the project (if not already
configured), builds the **Release** configuration, and runs the unit tests. On
success it prints the path to the built executable.

### Options

| Switch / parameter | Effect |
|--------------------|--------|
| `-Config <name>`   | Build a different configuration (`Release` (default), `Debug`, `MinSizeRel`, `RelWithDebInfo`). |
| `-NoTests`         | Skip the unit-test run after the build. |
| `-Clean`           | Delete the `build\` directory first for a from-scratch build. |
| `-Exe`             | Launch the built `RDSM.exe` after a successful build. |

Examples:

```
.\build.ps1 -Exe                 # build Release and launch the app
.\build.ps1 -Clean               # wipe build\ and rebuild from scratch
.\build.ps1 -Config Debug        # build the Debug configuration
.\build.ps1 -NoTests -Exe        # build Release, skip tests, launch the app
```

The script is idempotent: re-running it reuses the existing CMake cache, so
incremental builds are fast. Use `-Clean` only when you want a full rebuild.

## Building without build.ps1 (manual CMake)

The `build.ps1` script is a thin wrapper around these three commands. From the
project root:

```
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

1. **Configure** — `cmake -S . -B build -A x64` generates the build system into
   `build\` using the 64-bit Visual Studio generator. This step is idempotent;
   running it again reuses the existing cache.
2. **Build** — `cmake --build build --config Release` compiles the project.
   Replace `Release` with `Debug` (or another configuration) as needed.
3. **Test** (optional) — `ctest --test-dir build -C Release --output-on-failure`
   runs the unit tests. See `docs/TESTING.md` for details.

To build a different configuration, pass it to both the build and test steps:

```
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

To start from a clean slate, delete the `build\` directory before configuring:

```
Remove-Item -Recurse -Force build
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Output

- **App:** `build\bin\Release\RDSM.exe` (or `build\bin\<Config>\RDSM.exe` for
  other configurations). The Visual Studio multi-config generator appends the
  configuration name to the output directory.
- **Tests:** `build\bin\Release\RDSMTests.exe`.
- **Build info:** `build\generated\build_info.h` is regenerated on every build
  by the `BuildInfo` custom target, embedding the version and a 12-hour build
  timestamp into the exe (shown in the About dialog).

## Notes

- **Stop a running instance first.** If `RDSM.exe` is running, the linker cannot
  overwrite it and the build fails. `build.ps1` stops it automatically; when
  building manually, close the app (or `Stop-Process -Name RDSM -Force`) first.
- **No linter.** There is no separate lint step; the build uses `/W4 /permissive-`
  and is expected to compile with no warnings.
- **GUI verification.** The unit tests cover only the non-GUI logic in
  `src/shares.cpp`. The window and dialogs are verified by running the exe
  (see `docs/TESTING.md`).
