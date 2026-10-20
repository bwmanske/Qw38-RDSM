#include "test_framework.h"
#include "shares.h"

#include <string>

using rdsmon::queryShare;

// C: exists on every Windows machine. queryShare appends a trailing backslash,
// so "C:" is queried as "C:\".
TEST(queryShare_localDriveSucceeds) {
    rdsmon::ShareResult r = queryShare(L"C:");
    CHECK(r.ok);
    CHECK(r.totalBytes > 0);
    CHECK(r.freeBytes <= r.totalBytes);
    CHECK(r.usedBytes == r.totalBytes - r.freeBytes);
    CHECK(r.freePercent >= 0.0);
    CHECK(r.freePercent <= 100.0);
    CHECK(r.error.empty());
}

// A subdirectory that does not exist on C: fails fast (ERROR_PATH_NOT_FOUND),
// so this does not depend on any network share being reachable.
TEST(queryShare_missingPathFails) {
    rdsmon::ShareResult r = queryShare(L"C:\\rdsmon_no_such_dir_xyz_12345");
    CHECK(!r.ok);
    CHECK(!r.error.empty());
    CHECK(r.totalBytes == 0);
    CHECK(r.freeBytes == 0);
}

// A path that already ends in a backslash must not get a second one appended;
// it should query the same volume as the no-backslash form.
TEST(queryShare_trailingBackslash) {
    rdsmon::ShareResult r = queryShare(L"C:\\");
    CHECK(r.ok);
    CHECK(r.totalBytes > 0);
    CHECK(r.error.empty());
}

// The configured name is preserved on the result regardless of outcome.
TEST(queryShare_preservesName) {
    rdsmon::ShareResult ok = queryShare(L"C:");
    CHECK_EQ(ok.name, std::wstring(L"C:"));
    rdsmon::ShareResult bad = queryShare(L"C:\\rdsmon_no_such_dir_xyz_12345");
    CHECK_EQ(bad.name, std::wstring(L"C:\\rdsmon_no_such_dir_xyz_12345"));
}
