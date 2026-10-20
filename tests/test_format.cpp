#include "test_framework.h"
#include "shares.h"

#include <string>

using rdsmon::formatBytes;
using rdsmon::formatPercent;

TEST(formatBytes_belowKilobyte) {
    CHECK_EQ(formatBytes(0), std::wstring(L"0 B"));
    CHECK_EQ(formatBytes(1), std::wstring(L"1 B"));
    CHECK_EQ(formatBytes(1023), std::wstring(L"1023 B"));
}

TEST(formatBytes_kilobytes) {
    CHECK_EQ(formatBytes(1024), std::wstring(L"1.00 KB"));
    CHECK_EQ(formatBytes(2048), std::wstring(L"2.00 KB"));
}

TEST(formatBytes_megabytes) {
    CHECK_EQ(formatBytes(1024u * 1024u), std::wstring(L"1.00 MB"));
    CHECK_EQ(formatBytes(5u * 1024u * 1024u), std::wstring(L"5.00 MB"));
}

TEST(formatBytes_gigabytes) {
    CHECK_EQ(formatBytes(1024ull * 1024ull * 1024ull), std::wstring(L"1.00 GB"));
}

TEST(formatBytes_terabytes) {
    CHECK_EQ(formatBytes(1024ull * 1024ull * 1024ull * 1024ull), std::wstring(L"1.00 TB"));
}

TEST(formatBytes_petabytes) {
    CHECK_EQ(formatBytes(1024ull * 1024ull * 1024ull * 1024ull * 1024ull),
             std::wstring(L"1.00 PB"));
}

TEST(formatPercent_basic) {
    CHECK_EQ(formatPercent(0.0), std::wstring(L"0.0%"));
    CHECK_EQ(formatPercent(50.0), std::wstring(L"50.0%"));
    CHECK_EQ(formatPercent(100.0), std::wstring(L"100.0%"));
}

TEST(formatPercent_oneDecimal) {
    CHECK_EQ(formatPercent(42.0), std::wstring(L"42.0%"));
    CHECK_EQ(formatPercent(33.33), std::wstring(L"33.3%"));
}

// Just below a unit boundary the value rounds up to the next unit at 2 decimals.
TEST(formatBytes_boundaryRounding) {
    // 1023 * 1024 bytes = exactly 1023.00 KB.
    CHECK_EQ(formatBytes(1023u * 1024u), std::wstring(L"1023.00 KB"));
    // 1024 * 1024 - 1 bytes = 1023.999... KB, which rounds to 1024.00 KB.
    CHECK_EQ(formatBytes(1024u * 1024u - 1u), std::wstring(L"1024.00 KB"));
}
