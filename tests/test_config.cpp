#include "test_framework.h"
#include "shares.h"

#include <windows.h>

#include <string>
#include <vector>

namespace {

// Points APPDATA at a fresh temp directory so the config tests never touch the
// user's real %APPDATA%\RDSM\shares.json. The temp dir is created first because
// ensureConfigDir() uses CreateDirectoryW, which only creates the final path
// component (the real %APPDATA% always exists, so this mirrors production).
// Also resets the custom config file name to the default (shares.json) so each
// test starts from a clean slate. Returns the temp dir that was set.
std::wstring freshTempAppData() {
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    static int counter = 0;
    wchar_t unique[64] = {};
    swprintf_s(unique, 64, L"rdsmon_test_%lu_%d",
               static_cast<unsigned long>(GetCurrentProcessId()), ++counter);
    std::wstring dir = std::wstring(tempPath) + unique;
    CreateDirectoryW(dir.c_str(), nullptr);
    SetEnvironmentVariableW(L"APPDATA", dir.c_str());
    rdsmon::setConfigFileName(L"");
    return dir;
}

// Sets up a fresh temp APPDATA, ensures the RDSM config dir exists, and returns
// the config file path. Use this before planting raw config-file contents with
// writeRawFile (which, unlike saveConfig, does not create the directory).
std::wstring freshConfigPath() {
    const std::wstring appData = freshTempAppData();
    rdsmon::ensureConfigDir();
    return rdsmon::configFilePath();
}

// Writes raw UTF-8 bytes to a wide path (replacing any existing file). Used to
// plant specific config-file contents (bare arrays, invalid JSON, etc.) that
// saveConfig() would not produce.
bool writeRawFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)
                     && written == static_cast<DWORD>(data.size());
    CloseHandle(h);
    return ok;
}

// Reads the raw bytes of a wide path into a UTF-8 string.
bool readRawFile(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0;
    if (ok) {
        out.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(h, out.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr)
             && read == static_cast<DWORD>(size.QuadPart);
    }
    CloseHandle(h);
    return ok;
}

} // namespace

TEST(configDir_isAppDataRdsmon) {
    const std::wstring appData = freshTempAppData();
    CHECK_EQ(rdsmon::configDir(), appData + L"\\RDSM");
}

TEST(configFilePath_isSharesJson) {
    freshTempAppData();
    CHECK_EQ(rdsmon::configFilePath(), rdsmon::configDir() + L"\\shares.json");
}

// A custom file name (from --JSON) is placed in the default config directory.
TEST(configFilePath_customName) {
    freshTempAppData();
    rdsmon::setConfigFileName(L"myconfig.json");
    CHECK(rdsmon::hasCustomConfigFile());
    CHECK_EQ(rdsmon::configFilePath(), rdsmon::configDir() + L"\\myconfig.json");
}

TEST(ensureConfigDir_createsDirectory) {
    const std::wstring appData = freshTempAppData();
    const std::wstring dir = appData + L"\\RDSM";
    RemoveDirectoryW(dir.c_str()); // no-op if it does not exist
    CHECK(rdsmon::ensureConfigDir());
    const DWORD attr = GetFileAttributesW(dir.c_str());
    CHECK(attr != INVALID_FILE_ATTRIBUTES);
    CHECK((attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

TEST(saveLoad_roundTrips) {
    freshTempAppData();
    rdsmon::Config config;
    config.shares = {
        L"\\\\server\\share",
        L"C:",
        L"\\\\other\\data",
    };
    CHECK(rdsmon::saveConfig(config));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK_EQ(loaded.shares.size(), config.shares.size());
    for (std::size_t i = 0; i < config.shares.size(); ++i) {
        CHECK_EQ(loaded.shares[i], config.shares[i]);
    }
}

TEST(loadConfig_seedsDefaultsWhenMissing) {
    const std::wstring appData = freshTempAppData();
    DeleteFileW((appData + L"\\RDSM\\shares.json").c_str()); // no-op if absent
    const rdsmon::Config loaded = rdsmon::loadConfig();
    // The original RDSM.ps1 seeded 6 shares.
    CHECK_EQ(loaded.shares.size(), static_cast<std::size_t>(6));
    const DWORD attr = GetFileAttributesW((appData + L"\\RDSM\\shares.json").c_str());
    CHECK(attr != INVALID_FILE_ATTRIBUTES);
}

// A bare top-level JSON array (no "shares" wrapper) is accepted on read.
TEST(loadConfig_acceptsBareArray) {
    const std::wstring path = freshConfigPath();
    CHECK(writeRawFile(path, "[\"C:\", \"D:\"]"));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK_EQ(loaded.shares.size(), static_cast<std::size_t>(2));
    if (loaded.shares.size() == 2) {
        CHECK_EQ(loaded.shares[0], std::wstring(L"C:"));
        CHECK_EQ(loaded.shares[1], std::wstring(L"D:"));
    }
}

// A file that exists but is not valid JSON returns an empty config and is NOT
// clobbered (the original bytes are preserved).
TEST(loadConfig_invalidJsonReturnsEmptyAndNotClobbered) {
    const std::wstring path = freshConfigPath();
    const std::string invalid = "{ this is not valid json ]";
    CHECK(writeRawFile(path, invalid));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK(loaded.shares.empty());
    std::string onDisk;
    CHECK(readRawFile(path, onDisk));
    CHECK_EQ(onDisk, invalid);
}

// Non-ASCII share names survive a save/load round-trip (UTF-8 on disk).
// \u escapes are used so the test does not depend on the source file encoding.
TEST(loadConfig_unicodeRoundTrip) {
    freshTempAppData();
    rdsmon::Config config;
    config.shares = {
        L"\\\\server\\caf\u00e9",          // café
        L"\\\\server\\\u65e5\u672c\u8a9e", // 日本語
        L"\\\\server\\na\u00efve",         // naïve
    };
    CHECK(rdsmon::saveConfig(config));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK_EQ(loaded.shares.size(), config.shares.size());
    for (std::size_t i = 0; i < config.shares.size(); ++i) {
        CHECK_EQ(loaded.shares[i], config.shares[i]);
    }
}

// Non-string elements in the array are skipped; only strings are returned.
TEST(loadConfig_skipsNonStringElements) {
    const std::wstring path = freshConfigPath();
    CHECK(writeRawFile(path, "[\"C:\", 42, {\"x\": 1}, \"D:\", null]"));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK_EQ(loaded.shares.size(), static_cast<std::size_t>(2));
    if (loaded.shares.size() == 2) {
        CHECK_EQ(loaded.shares[0], std::wstring(L"C:"));
        CHECK_EQ(loaded.shares[1], std::wstring(L"D:"));
    }
}

// A JSON object that lacks a "shares" array yields an empty share list.
TEST(loadConfig_objectWithoutSharesKeyReturnsEmpty) {
    const std::wstring path = freshConfigPath();
    CHECK(writeRawFile(path, "{\"foo\": \"bar\"}"));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK(loaded.shares.empty());
}

// An empty share list round-trips (the file is written with an empty array).
TEST(saveConfig_emptyList) {
    freshTempAppData();
    rdsmon::Config config; // empty shares
    CHECK(rdsmon::saveConfig(config));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK(loaded.shares.empty());
}

// UI state (column widths, window size, window position) round-trips.
TEST(saveLoad_uiStateRoundTrip) {
    freshTempAppData();
    rdsmon::Config config;
    config.shares = {L"C:"};
    config.columnWidths = {300, 120, 120, 120, 150};
    config.windowWidth = 1000;
    config.windowHeight = 600;
    config.hasWindowSize = true;
    config.windowX = 50;
    config.windowY = 70;
    config.hasWindowPosition = true;
    CHECK(rdsmon::saveConfig(config));

    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK_EQ(loaded.columnWidths.size(), static_cast<std::size_t>(5));
    for (std::size_t i = 0; i < config.columnWidths.size(); ++i) {
        CHECK_EQ(loaded.columnWidths[i], config.columnWidths[i]);
    }
    CHECK(loaded.hasWindowSize);
    CHECK_EQ(loaded.windowWidth, 1000);
    CHECK_EQ(loaded.windowHeight, 600);
    CHECK(loaded.hasWindowPosition);
    CHECK_EQ(loaded.windowX, 50);
    CHECK_EQ(loaded.windowY, 70);
}

// When no UI state was saved, the loaded config reports it as absent.
TEST(loadConfig_uiStateAbsentWhenNotSaved) {
    freshTempAppData();
    rdsmon::Config config;
    config.shares = {L"C:"};
    CHECK(rdsmon::saveConfig(config));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK(loaded.columnWidths.empty());
    CHECK(!loaded.hasWindowSize);
    CHECK(!loaded.hasWindowPosition);
}

// A custom file that does not exist starts with no data (no seeding) and the
// file is created (initialized) with the empty config.
TEST(loadConfig_customFileMissingStartsEmptyAndCreates) {
    const std::wstring appData = freshTempAppData();
    rdsmon::setConfigFileName(L"custom.json");
    const std::wstring path = appData + L"\\RDSM\\custom.json";
    DeleteFileW(path.c_str()); // no-op if absent
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK(loaded.shares.empty()); // no seeding for custom files
    const DWORD attr = GetFileAttributesW(path.c_str());
    CHECK(attr != INVALID_FILE_ATTRIBUTES); // file was created
}

// A custom file that is invalid JSON starts with no data and is not clobbered.
TEST(loadConfig_customFileInvalidStartsEmptyAndNotClobbered) {
    const std::wstring appData = freshTempAppData();
    rdsmon::setConfigFileName(L"custom.json");
    rdsmon::ensureConfigDir();
    const std::wstring path = appData + L"\\RDSM\\custom.json";
    const std::string invalid = "{ not valid ]";
    CHECK(writeRawFile(path, invalid));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK(loaded.shares.empty());
    std::string onDisk;
    CHECK(readRawFile(path, onDisk));
    CHECK_EQ(onDisk, invalid);
}

// When APPDATA is empty, configDir() falls back to the bare "RDSM".
TEST(configDir_fallbackWhenAppDataEmpty) {
    SetEnvironmentVariableW(L"APPDATA", L"");
    CHECK_EQ(rdsmon::configDir(), std::wstring(L"RDSM"));
}

// autoRefreshMinutes round-trips through save/load.
TEST(saveLoad_autoRefreshRoundTrip) {
    freshTempAppData();
    rdsmon::Config config;
    config.shares = {L"C:"};
    config.autoRefreshMinutes = 45;
    CHECK(rdsmon::saveConfig(config));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK_EQ(loaded.autoRefreshMinutes, 45);
}

// When the field is absent from the file, the default (15) is used.
TEST(loadConfig_autoRefreshDefaultWhenAbsent) {
    const std::wstring path = freshConfigPath();
    CHECK(writeRawFile(path, "{\"shares\": [\"C:\"]}"));
    const rdsmon::Config loaded = rdsmon::loadConfig();
    CHECK_EQ(loaded.autoRefreshMinutes, 15);
}

// Out-of-range values (below 1 or above 3600) keep the default (15).
TEST(loadConfig_autoRefreshOutOfRangeKeepsDefault) {
    const std::wstring path = freshConfigPath();
    CHECK(writeRawFile(path, "{\"shares\": [], \"autoRefreshMinutes\": 0}"));
    CHECK_EQ(rdsmon::loadConfig().autoRefreshMinutes, 15);
    CHECK(writeRawFile(path, "{\"shares\": [], \"autoRefreshMinutes\": 3601}"));
    CHECK_EQ(rdsmon::loadConfig().autoRefreshMinutes, 15);
    CHECK(writeRawFile(path, "{\"shares\": [], \"autoRefreshMinutes\": -5}"));
    CHECK_EQ(rdsmon::loadConfig().autoRefreshMinutes, 15);
}

// The boundary values 1 and 3600 are accepted.
TEST(loadConfig_autoRefreshBoundaryValues) {
    const std::wstring path = freshConfigPath();
    CHECK(writeRawFile(path, "{\"shares\": [], \"autoRefreshMinutes\": 1}"));
    CHECK_EQ(rdsmon::loadConfig().autoRefreshMinutes, 1);
    CHECK(writeRawFile(path, "{\"shares\": [], \"autoRefreshMinutes\": 3600}"));
    CHECK_EQ(rdsmon::loadConfig().autoRefreshMinutes, 3600);
}
