#include "test_framework.h"
#include "shares.h"

#include <string>
#include <vector>

using rdsmon::validateShares;

// A valid drive letter and a valid UNC path produce no errors.
TEST(validateShares_validSharesNoErrors) {
    const std::vector<std::wstring> shares = {
        L"C:",
        L"D:",
        L"\\\\server\\share",
        L"\\\\My-PC_01\\Data",
    };
    const std::vector<std::wstring> errors = validateShares(shares);
    CHECK_EQ(errors.size(), shares.size());
    for (const std::wstring& e : errors) {
        CHECK(e.empty());
    }
}

// A malformed share (not a drive letter or a well-formed UNC path) is flagged.
TEST(validateShares_malformedFlagged) {
    const std::vector<std::wstring> shares = {
        L"C:",
        L"not a share",        // neither a drive letter nor a UNC path
        L"\\\\server",         // UNC with no \\share part
        L"\\\\\\share",        // three leading backslashes (empty server)
        L"\\\\server\\\\share",// double backslash between server and share
        L"\\\\server\\",       // UNC with an empty share name
    };
    const std::vector<std::wstring> errors = validateShares(shares);
    CHECK(errors[0].empty());   // C: is valid
    CHECK(!errors[1].empty());  // "not a share"
    CHECK(!errors[2].empty());  // \\server (no share)
    CHECK(!errors[3].empty());  // \\\share (empty server)
    CHECK(!errors[4].empty());  // \\server\\share (double backslash)
    CHECK(!errors[5].empty());  // \\server\ (empty share)
}

// Drive letters A: and B: are not valid (only C: through Z:); lowercase is ok.
// (Distinct letters are used so the duplicate check does not also fire.)
TEST(validateShares_driveLetterRange) {
    const std::vector<std::wstring> shares = {L"A:", L"B:", L"C:", L"Z:", L"d:"};
    const std::vector<std::wstring> errors = validateShares(shares);
    CHECK(!errors[0].empty()); // A: invalid
    CHECK(!errors[1].empty()); // B: invalid
    CHECK(errors[2].empty());  // C: valid
    CHECK(errors[3].empty());  // Z: valid
    CHECK(errors[4].empty());  // d: valid (lowercase)
}

// A duplicate share name (case-insensitive) flags the first occurrence only.
TEST(validateShares_duplicateFlagged) {
    const std::vector<std::wstring> shares = {
        L"\\\\server\\share",
        L"C:",
        L"\\\\SERVER\\SHARE", // duplicate of line 0 (case-insensitive)
    };
    const std::vector<std::wstring> errors = validateShares(shares);
    CHECK(!errors[0].empty()); // first occurrence flagged
    CHECK(errors[1].empty());  // C: fine
    CHECK(errors[2].empty());  // second occurrence not flagged (only the first)
}

// A line that is both malformed and a duplicate reports the malformed error
// (the earlier check in the pipeline wins).
TEST(validateShares_malformedTakesPrecedenceOverDuplicate) {
    const std::vector<std::wstring> shares = {L"bad", L"bad"};
    const std::vector<std::wstring> errors = validateShares(shares);
    CHECK(!errors[0].empty());
    CHECK(errors[0].find(L"Malformed") != std::wstring::npos);
}

// Share names containing invalid characters (or an empty component) are flagged.
TEST(validateShares_invalidNameChars) {
    const std::vector<std::wstring> shares = {
        L"\\\\server\\bad|name", // pipe is not a valid name character
        L"\\\\server\\",         // empty share name
        L"\\\\\\share",          // empty server name
    };
    const std::vector<std::wstring> errors = validateShares(shares);
    CHECK(!errors[0].empty());
    CHECK(!errors[1].empty());
    CHECK(!errors[2].empty());
}

// An empty list produces no errors.
TEST(validateShares_emptyList) {
    const std::vector<std::wstring> shares = {};
    const std::vector<std::wstring> errors = validateShares(shares);
    CHECK(errors.empty());
}
