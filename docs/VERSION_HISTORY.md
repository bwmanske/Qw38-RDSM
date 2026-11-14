# RDSM Version History

Per-version notes for RDSM. The current version is `RDSM_VERSION` in `CMakeLists.txt`
(single source of truth; surfaced in the About dialog, `build_info.h`, and the spec
header).

## v1.3 — dedicated worker thread (responsiveness)

All disk access (SMB queries + Drobo detection/queries) now runs on a single
persistent worker thread that lives for the whole app run. `refresh()` requests a
pass non-blocking (requests made while a pass is in flight coalesce into one extra
pass); posted results carry a pass sequence number, and the UI discards stale
results from superseded passes. The Edit Shares and Settings dialogs no longer stop
the worker, so opening them never blocks the UI thread on an in-flight query
(previously `stopWorker()` joined the in-flight worker on the UI thread).
`stopWorker()` is exit-path only; the worker checks the stop flag between shares,
so the exit join waits at most for the in-flight share's query.

## v1.2 — Free % bar color thresholds

The GDI+ free-space bar colors by how much space is left — green from 30% free and
up, amber from under 30% down to 15%, red below 15% (boundaries inclusive).
Previously green >50%, amber >20%, red ≤20%. Visually verified in the running exe:
bar colors at the new thresholds, and the About dialog shows Version 1.2.

## v1.1 — Drobo NAS support

For every share that is not a Windows drive letter, the worker thread checks whether
its server is a Drobo (TCP port 5000 status daemon; ~2 s timeout) and, if so, shows
the real device-level capacity from the pushed "NASD-XML-format" document instead of
the thin-provisioned SMB numbers. Detection results are cached per server in memory
until that server leaves the list or the app exits (a detection failure caches "not
a Drobo", so the timeout is paid once); the cache is pruned at each worker start and
only the worker touches it. A cached-Drobo share whose query fails on a later
refresh falls back to SMB for that cycle. Pure helpers are unit-tested in
`tests/test_drobo.cpp` (73 tests total). Live-verified against a real Drobo
(firmware 4.3.1): the numbers match Drobo Dashboard.

## v1.0 — config error detection and correction

A corrupt (unparseable) config file is not read; the app starts cleanly (default
shares for the default file, empty for a custom file), warns the user, and asks
permission to overwrite the file (declining keeps the data in memory only).
`autoRefreshMinutes` out of range (1–3600) falls back to 30. The Edit Shares dialog
runs an extensible error test (`rdsmon::validateShares`: malformed + duplicate
shares), shows the selected line's error in a red lower-left label; Save is grayed
out while the current list has an error, and Cancel/X are grayed out when the
initially loaded (saved) list itself has an error (closing would return to bad
data). Unit-tested in `tests/test_validate.cpp` + `tests/test_config.cpp`; the GUI
prompt/dialog are verified by running the exe.
