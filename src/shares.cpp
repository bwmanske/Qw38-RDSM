#include "shares.h"

#include <windows.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace rdsmon {
namespace {

// The custom config file name set via setConfigFileName(). Empty means use the
// default shares.json. Set once at startup before any load/save, so no locking
// is needed.
std::wstring g_customConfigName;

// Reads an entire file (by wide path) into a UTF-8 string.
bool readFileBytes(const std::wstring& path, std::string& out) {
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

// Writes a UTF-8 string to a file (by wide path), replacing any existing file.
bool writeFileBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)
               && written == static_cast<DWORD>(data.size());
    CloseHandle(h);
    return ok;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(len), '\0');
    if (len > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                            s.data(), len, nullptr, nullptr);
    }
    return s;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                  nullptr, 0);
    std::wstring w(static_cast<std::size_t>(len), L'\0');
    if (len > 0) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                            w.data(), len);
    }
    return w;
}

std::wstring lastErrorString() {
    DWORD err = GetLastError();
    LPWSTR msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                   | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::wstring result;
    if (msg) {
        result = msg;
        while (!result.empty()
               && (result.back() == L'\n' || result.back() == L'\r' || result.back() == L' ')) {
            result.pop_back();
        }
        LocalFree(msg);
    }
    if (result.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"Error %lu", err);
        result = buf;
    }
    return result;
}

// The shares from the original RDSM.ps1, used to seed the default config file
// the first time the app runs.
const std::vector<std::wstring>& defaultShares() {
    static const std::vector<std::wstring> shares = {
        L"\\\\Chip\\work",
        L"\\\\Dale\\work",
        L"\\\\bmanske-plex-pc\\Plex-I",
        L"\\\\bmanske-plex-pc\\Plex-J",
        L"\\\\bmanske-plex-pc\\Plex-K",
        L"\\\\bmanske-game-pc\\Game-D",
    };
    return shares;
}

} // namespace

void setConfigFileName(const std::wstring& name) {
    g_customConfigName = name;
}

bool hasCustomConfigFile() {
    return !g_customConfigName.empty();
}

std::wstring configDir() {
    wchar_t env[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", env, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::wstring(env) + L"\\RDSM";
    }
    return L"RDSM";
}

std::wstring configFilePath() {
    if (hasCustomConfigFile()) {
        return configDir() + L"\\" + g_customConfigName;
    }
    return configDir() + L"\\shares.json";
}

bool ensureConfigDir() {
    std::wstring dir = configDir();
    DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return true; // already exists
    }
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

Config loadConfig() {
    ensureConfigDir();
    Config config;

    std::string content;
    if (!readFileBytes(configFilePath(), content)) {
        // File missing.
        if (hasCustomConfigFile()) {
            // Custom file: start with no data and initialize (create) the file.
            saveConfig(config);
        } else {
            // Default file: seed with the original script's shares.
            config.shares = defaultShares();
            saveConfig(config);
        }
        return config;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(content);

        // shares: a bare top-level array, or an object with a "shares" array.
        const nlohmann::json* arr = nullptr;
        if (j.is_array()) {
            arr = &j;
        } else if (j.is_object() && j.contains("shares") && j["shares"].is_array()) {
            arr = &j["shares"];
        }
        if (arr) {
            config.shares.reserve(arr->size());
            for (const auto& el : *arr) {
                if (el.is_string()) {
                    config.shares.push_back(toWide(el.get<std::string>()));
                }
            }
        }

        // columnWidths: an array of integers.
        if (j.is_object() && j.contains("columnWidths") && j["columnWidths"].is_array()) {
            for (const auto& el : j["columnWidths"]) {
                if (el.is_number_integer()) {
                    config.columnWidths.push_back(el.get<int>());
                }
            }
        }

        // windowSize: an object with integer "width" and "height".
        if (j.is_object() && j.contains("windowSize") && j["windowSize"].is_object()) {
            const auto& ws = j["windowSize"];
            if (ws.contains("width") && ws["width"].is_number_integer()
                && ws.contains("height") && ws["height"].is_number_integer()) {
                config.windowWidth = ws["width"].get<int>();
                config.windowHeight = ws["height"].get<int>();
                config.hasWindowSize = true;
            }
        }

        // windowPosition: an object with integer "x" and "y".
        if (j.is_object() && j.contains("windowPosition") && j["windowPosition"].is_object()) {
            const auto& wp = j["windowPosition"];
            if (wp.contains("x") && wp["x"].is_number_integer()
                && wp.contains("y") && wp["y"].is_number_integer()) {
                config.windowX = wp["x"].get<int>();
                config.windowY = wp["y"].get<int>();
                config.hasWindowPosition = true;
            }
        }

        // autoRefreshMinutes: an integer (1-3600); out-of-range values keep the
        // default (15).
        if (j.is_object() && j.contains("autoRefreshMinutes")
            && j["autoRefreshMinutes"].is_number_integer()) {
            int v = j["autoRefreshMinutes"].get<int>();
            if (v >= 1 && v <= 3600) {
                config.autoRefreshMinutes = v;
            }
        }

        return config;
    } catch (const nlohmann::json::exception&) {
        // File exists but is not valid JSON. Do not clobber it; return empty.
        return Config{};
    }
}

bool saveConfig(const Config& config) {
    if (!ensureConfigDir()) {
        return false;
    }
    nlohmann::json j;
    j["shares"] = nlohmann::json::array();
    for (const auto& s : config.shares) {
        j["shares"].push_back(toUtf8(s));
    }
    if (!config.columnWidths.empty()) {
        j["columnWidths"] = nlohmann::json::array();
        for (int w : config.columnWidths) {
            j["columnWidths"].push_back(w);
        }
    }
    if (config.hasWindowSize) {
        j["windowSize"] = nlohmann::json::object();
        j["windowSize"]["width"] = config.windowWidth;
        j["windowSize"]["height"] = config.windowHeight;
    }
    if (config.hasWindowPosition) {
        j["windowPosition"] = nlohmann::json::object();
        j["windowPosition"]["x"] = config.windowX;
        j["windowPosition"]["y"] = config.windowY;
    }
    j["autoRefreshMinutes"] = config.autoRefreshMinutes;
    return writeFileBytes(configFilePath(), j.dump(2));
}

ShareResult queryShare(const std::wstring& share) {
    ShareResult r;
    r.name = share;

    std::wstring target = share;
    if (!target.empty() && target.back() != L'\\') {
        target += L'\\';
    }

    ULARGE_INTEGER freeAvail{}, total{}, totalFree{};
    if (GetDiskFreeSpaceExW(target.c_str(), &freeAvail, &total, &totalFree)) {
        r.ok = true;
        r.totalBytes = total.QuadPart;
        r.freeBytes = freeAvail.QuadPart;
        r.usedBytes = (total.QuadPart > freeAvail.QuadPart)
                          ? (total.QuadPart - freeAvail.QuadPart)
                          : 0;
        if (total.QuadPart > 0) {
            r.freePercent = static_cast<double>(freeAvail.QuadPart)
                            / static_cast<double>(total.QuadPart) * 100.0;
        }
    } else {
        r.ok = false;
        r.error = lastErrorString();
    }
    return r;
}

std::wstring formatBytes(std::uint64_t bytes) {
    static const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buf[64];
    if (unit == 0) {
        swprintf_s(buf, L"%llu %s", static_cast<unsigned long long>(bytes), units[0]);
    } else {
        swprintf_s(buf, L"%.2f %s", value, units[unit]);
    }
    return buf;
}

std::wstring formatPercent(double pct) {
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f%%", pct);
    return buf;
}

} // namespace rdsmon
