#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace rdsmon {

// Result of querying a single share's free space.
struct ShareResult {
    std::wstring name;                 // the share path as configured
    bool ok = false;                   // true if the query succeeded
    std::uint64_t totalBytes = 0;      // total size of the volume
    std::uint64_t usedBytes = 0;       // total - free
    std::uint64_t freeBytes = 0;       // free bytes available to the user
    double freePercent = 0.0;          // freeBytes / totalBytes * 100
    std::wstring error;                // human readable error when ok == false
};

// The full persisted configuration: the share list plus UI state (column
// widths, window size, and window position) and settings (auto-refresh
// interval). The UI state fields are optional; hasWindowSize /
// hasWindowPosition indicate whether they were present in the file.
// columnWidths is empty when not saved. autoRefreshMinutes always has a value
// (default 15) and is the minutes between automatic checks (1-3600).
struct Config {
    std::vector<std::wstring> shares;
    std::vector<int> columnWidths;     // one entry per column; empty if not saved
    int windowWidth = 0;
    int windowHeight = 0;
    int windowX = 0;
    int windowY = 0;
    bool hasWindowSize = false;
    bool hasWindowPosition = false;
    int autoRefreshMinutes = 15;       // minutes between automatic checks (1-3600)
};

// The outcome of loadConfig(). The caller uses Corrupt to warn the user that the
// file could not be read and to ask permission to rewrite it.
enum class LoadStatus {
    Ok,      // file read successfully (or seeded/initialized on first run)
    Corrupt, // file existed but was not valid JSON; no data was read from it
};

// Returns %AppData%\RDSM (e.g. C:\Users\<user>\AppData\Roaming\RDSM).
std::wstring configDir();

// Returns the config file path. If a custom file name has been set via
// setConfigFileName(), it is <configDir>\<customName>; otherwise
// <configDir>\shares.json.
std::wstring configFilePath();

// Sets the custom config file name (from the --JSON command line option). The
// file is placed in the default config directory. Pass an empty string to use
// the default shares.json. Must be called before the first loadConfig/saveConfig.
void setConfigFileName(const std::wstring& name);

// True if a custom config file name has been set.
bool hasCustomConfigFile();

// Creates the config directory if it does not already exist.
// Returns true on success or if it already existed.
bool ensureConfigDir();

// Loads the full configuration from the config file.
//   Default file: if missing, seeds the 6 default shares (no UI state) and
//     writes the file. If the structure is corrupt (not valid JSON), no data is
//     read; the config is seeded with the default shares and *status is set to
//     Corrupt (the file is NOT written here — the caller asks the user first).
//   Custom file:  if missing, returns an empty config and writes (initializes)
//     the file. If the structure is corrupt, no data is read; the config is
//     empty and *status is set to Corrupt (file NOT written here).
// *status (if non-null) receives the load outcome.
Config loadConfig(LoadStatus* status = nullptr);

// Validates a share list. Returns a vector parallel to `shares` where entry i
// is a human-readable error for shares[i] (an empty string means the line is
// valid). Validation is an extensible pipeline of checks (see shares.cpp); a
// line reports the first error assigned to it, in check order.
std::vector<std::wstring> validateShares(const std::vector<std::wstring>& shares);

// Saves the full configuration to the config file. Returns true on success.
bool saveConfig(const Config& config);

// Queries a single share using GetDiskFreeSpaceEx. The share path is used as
// given; a trailing backslash is appended if missing (the API prefers it).
ShareResult queryShare(const std::wstring& share);

// Formats a byte count as a human readable string, e.g. "1.23 TB", "456.78 GB".
std::wstring formatBytes(std::uint64_t bytes);

// Formats a percentage with one decimal place, e.g. "42.3%".
std::wstring formatPercent(double pct);

} // namespace rdsmon
