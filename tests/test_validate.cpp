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

// Drive letters must be exactly one letter (C-Z) plus a colon. A trailing space,
// two letters, an extra character, a bare colon, or a bare letter are all
// malformed. (Distinct strings are used so the duplicate check does not fire.)
TEST(validateShares_driveLetterEdgeCases) {
    const std::vector<std::wstring> shares = {
        L"C: ",  // trailing space
        L"CC:",  // two letters
        L"C:d",  // extra character after the colon
        L":",    // bare colon
        L"C",    // bare letter, no colon
    };
    const std::vector<std::wstring> errors = validateShares(shares);
    for (const std::wstring& e : errors) {
        CHECK(!e.empty());
    }
}

// A UNC path must be exactly \\server\share: a subdirectory (a third backslash),
// a trailing backslash, or a single leading backslash are all malformed.
TEST(validateShares_uncPathEdgeCases) {
    const std::vector<std::wstring> shares = {
        L"\\\\server\\share\\sub", // subdirectory (extra backslash)
        L"\\\\server\\share\\",    // trailing backslash
        L"\\server\\share",        // single leading backslash
    };
    const std::vector<std::wstring> errors = validateShares(shares);
    for (const std::wstring& e : errors) {
        CHECK(!e.empty());
    }
}

// Valid UNC names may contain spaces and the allowed punctuation; the minimum
// path \\a\b (one-char server and share) is valid.
TEST(validateShares_validUncNames) {
    const std::vector<std::wstring> shares = {
        L"\\\\my server\\my share", // spaces in server and share
        L"\\\\a\\b",                // minimum one-character names
        L"\\\\My-PC_01\\Data (2)",  // punctuation: - _ space ( )
    };
    const std::vector<std::wstring> errors = validateShares(shares);
    for (const std::wstring& e : errors) {
        CHECK(e.empty());
    }
}

// A case-insensitive duplicate of a valid drive letter (C: and c:) flags the
// first occurrence only; the second occurrence and the other share are clean.
TEST(validateShares_duplicateOfValidShare) {
    const std::vector<std::wstring> shares = {
        L"C:",
        L"D:",
        L"c:", // duplicate of line 0 (case-insensitive)
    };
    const std::vector<std::wstring> errors = validateShares(shares);
    CHECK(!errors[0].empty()); // first occurrence flagged
    CHECK(errors[1].empty());  // D: fine
    CHECK(errors[2].empty());  // second occurrence not flagged
}
