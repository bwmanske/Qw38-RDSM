#include "test_framework.h"
#include "drobo.h"

#include <string>
#include <vector>

using rdsmon::isDriveShare;
using rdsmon::serverFromUnc;
using rdsmon::normalizeServer;
using rdsmon::pruneDroboCache;
using rdsmon::parseDroboXml;
using rdsmon::isDroboFrame;

// Drive letters C: through Z: (either case) are Windows drives.
TEST(isDriveShare_validLetters) {
    CHECK(isDriveShare(L"C:"));
    CHECK(isDriveShare(L"Z:"));
    CHECK(isDriveShare(L"c:"));
    CHECK(isDriveShare(L"z:"));
}

// A:, B:, malformed strings, and UNC paths are not drive letters.
TEST(isDriveShare_notDrives) {
    CHECK(!isDriveShare(L"A:"));
    CHECK(!isDriveShare(L"B:"));
    CHECK(!isDriveShare(L"C"));      // no colon
    CHECK(!isDriveShare(L"CC:"));    // two letters
    CHECK(!isDriveShare(L"C:d"));    // extra character
    CHECK(!isDriveShare(L":"));      // bare colon
    CHECK(!isDriveShare(L""));       // empty
    CHECK(!isDriveShare(L"\\\\server\\share"));
}

// The server name is extracted from well-formed UNC paths.
TEST(serverFromUnc_validPaths) {
    CHECK_EQ(serverFromUnc(L"\\\\server\\share"), std::wstring(L"server"));
    CHECK_EQ(serverFromUnc(L"\\\\My-PC_01\\Data (2)"), std::wstring(L"My-PC_01"));
    CHECK_EQ(serverFromUnc(L"\\\\a\\b"), std::wstring(L"a"));
}

// Malformed paths yield an empty server name.
TEST(serverFromUnc_malformedPaths) {
    CHECK(serverFromUnc(L"C:").empty());              // drive letter
    CHECK(serverFromUnc(L"\\server\\share").empty()); // single leading backslash
    CHECK(serverFromUnc(L"\\\\server").empty());      // no \\share part
    CHECK(serverFromUnc(L"\\\\server\\").empty());    // empty share name
    CHECK(serverFromUnc(L"\\\\\\share").empty());     // empty server name
    CHECK(serverFromUnc(L"\\\\server\\share\\sub").empty()); // subdirectory
}

// normalizeServer lowercases for case-insensitive cache keys.
TEST(normalizeServer_lowercases) {
    CHECK_EQ(normalizeServer(L"My-PC_01"), std::wstring(L"my-pc_01"));
    CHECK_EQ(normalizeServer(L"already-lower"), std::wstring(L"already-lower"));
}

// pruneDroboCache removes entries whose server is no longer in the list
// (case-insensitively) and keeps the rest.
TEST(pruneDroboCache_removesMissingServers) {
    rdsmon::DroboCache cache;
    cache[normalizeServer(L"Drobo")] = rdsmon::DroboInfo{true, L"192.168.1.50"};
    cache[normalizeServer(L"Chip")] = rdsmon::DroboInfo{false, {}};

    const std::vector<std::wstring> shares = {L"C:", L"\\\\CHIP\\work"};
    pruneDroboCache(cache, shares);

    CHECK_EQ(cache.size(), static_cast<std::size_t>(1));
    CHECK(cache.find(normalizeServer(L"Chip")) != cache.end());
    CHECK(cache.find(normalizeServer(L"Drobo")) == cache.end());
}

// A list with no UNC shares (only drive letters) empties the cache.
TEST(pruneDroboCache_allRemovedWhenNoUncShares) {
    rdsmon::DroboCache cache;
    cache[normalizeServer(L"Drobo")] = rdsmon::DroboInfo{true, L"192.168.1.50"};

    const std::vector<std::wstring> shares = {L"C:", L"D:"};
    pruneDroboCache(cache, shares);

    CHECK(cache.empty());
}

// The capacity fields are extracted from a real NASD XML document (values from
// the droboports wiki's DroboFS sample).
TEST(parseDroboXml_validDocument) {
    const std::string xml =
        "DRINASD<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<ESATMUpdate>\n"
        "  <mTotalCapacityProtected>5928828272640</mTotalCapacityProtected>\n"
        "  <mUsedCapacityProtected>1751838081024</mUsedCapacityProtected>\n"
        "  <mFreeCapacityProtected>4176990191616</mFreeCapacityProtected>\n"
        "</ESATMUpdate>";
    rdsmon::DroboCapacity cap;
    CHECK(parseDroboXml(xml, cap));
    CHECK_EQ(cap.totalBytes, 5928828272640ULL);
    CHECK_EQ(cap.usedBytes, 1751838081024ULL);
    CHECK_EQ(cap.freeBytes, 4176990191616ULL);
}

// Whitespace around the numbers is allowed.
TEST(parseDroboXml_whitespaceAroundNumbers) {
    const std::string xml =
        "<ESATMUpdate>\n"
        "  <mTotalCapacityProtected>   123 </mTotalCapacityProtected>\n"
        "  <mUsedCapacityProtected>\t45\t</mUsedCapacityProtected>\n"
        "  <mFreeCapacityProtected>78</mFreeCapacityProtected>\n"
        "</ESATMUpdate>";
    rdsmon::DroboCapacity cap;
    CHECK(parseDroboXml(xml, cap));
    CHECK_EQ(cap.totalBytes, 123ULL);
    CHECK_EQ(cap.usedBytes, 45ULL);
    CHECK_EQ(cap.freeBytes, 78ULL);
}

// A missing tag fails the parse.
TEST(parseDroboXml_missingTag) {
    const std::string xml =
        "<ESATMUpdate>\n"
        "  <mTotalCapacityProtected>100</mTotalCapacityProtected>\n"
        "  <mUsedCapacityProtected>50</mUsedCapacityProtected>\n"
        "</ESATMUpdate>"; // no mFreeCapacityProtected
    rdsmon::DroboCapacity cap;
    CHECK(!parseDroboXml(xml, cap));
}

// A non-numeric value fails the parse.
TEST(parseDroboXml_nonNumericValue) {
    const std::string xml =
        "<ESATMUpdate>\n"
        "  <mTotalCapacityProtected>100</mTotalCapacityProtected>\n"
        "  <mUsedCapacityProtected>lots</mUsedCapacityProtected>\n"
        "  <mFreeCapacityProtected>50</mFreeCapacityProtected>\n"
        "</ESATMUpdate>";
    rdsmon::DroboCapacity cap;
    CHECK(!parseDroboXml(xml, cap));
}

// mTotalCapacityUnprotected must not be mistaken for mTotalCapacityProtected.
TEST(parseDroboXml_unprotectedTagNotConfused) {
    const std::string xml =
        "<ESATMUpdate>\n"
        "  <mTotalCapacityUnprotected>0</mTotalCapacityUnprotected>\n"
        "</ESATMUpdate>";
    rdsmon::DroboCapacity cap;
    CHECK(!parseDroboXml(xml, cap));
}

// An empty tag body fails the parse.
TEST(parseDroboXml_emptyValue) {
    const std::string xml =
        "<ESATMUpdate>\n"
        "  <mTotalCapacityProtected></mTotalCapacityProtected>\n"
        "  <mUsedCapacityProtected>50</mUsedCapacityProtected>\n"
        "  <mFreeCapacityProtected>50</mFreeCapacityProtected>\n"
        "</ESATMUpdate>";
    rdsmon::DroboCapacity cap;
    CHECK(!parseDroboXml(xml, cap));
}

// The DRINASD magic in the header identifies a Drobo frame (current firmwares:
// the body starts directly with XML). Header bytes from a real Drobo 4.3.1.
TEST(isDroboFrame_magicInHeader) {
    const std::string header = "DRINASD\0\x01\x01\0\0\0\0\x1C\xA1"; // 16 bytes
    CHECK(isDroboFrame(header, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
}

// The DRINASDa / DRINASD9 variants also start with DRINASD.
TEST(isDroboFrame_magicVariants) {
    CHECK(isDroboFrame("DRINASDa", "<ESATMUpdate>"));
    CHECK(isDroboFrame("DRINASD9", "<ESATMUpdate>"));
}

// Older firmwares reportedly put the magic at the start of the body instead.
TEST(isDroboFrame_magicInBody) {
    const std::string header = "\x01\x02"; // no magic in the header
    CHECK(isDroboFrame(header, "DRINASD<?xml version=\"1.0\"?>"));
}

// Anything else on port 5000 is not a Drobo (including a too-short prefix).
TEST(isDroboFrame_notADrobo) {
    CHECK(!isDroboFrame("HTTP/1.1 200 OK\r\n", "<html>"));
    CHECK(!isDroboFrame("", ""));
    CHECK(!isDroboFrame("DRINAS", "<?xml")); // six chars, not the seven-char magic
}
