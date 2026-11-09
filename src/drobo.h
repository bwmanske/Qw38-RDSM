#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rdsmon {

// Drobo NAS volumes are thin-provisioned: the SMB share reports the virtual
// volume size (e.g. 64 TiB), not the physical capacity. The real numbers come
// from the Drobo status daemon, which listens on TCP port 5000 and pushes an
// XML document ("NASD-XML-format", see the droboports wiki) as soon as a
// connection is established — no command needs to be sent. The document's
// mTotalCapacityProtected / mUsedCapacityProtected / mFreeCapacityProtected
// fields are device-level (the whole NAS, not per share) and match what Drobo
// Dashboard shows.

struct DroboCapacity {
    std::uint64_t totalBytes = 0;
    std::uint64_t usedBytes = 0;
    std::uint64_t freeBytes = 0;
};

// True if the share is a Windows drive letter ("C:" through "Z:", either case).
bool isDriveShare(const std::wstring& share);

// Returns the server name of a well-formed UNC path ("\\server\share" ->
// "server"), or an empty string otherwise.
std::wstring serverFromUnc(const std::wstring& share);

// Lowercased copy, for case-insensitive cache keys (Windows share names and
// hostnames are case-insensitive).
std::wstring normalizeServer(const std::wstring& s);

// Per-server detection result, cached in memory for the app's lifetime. The IP
// is the address resolved at detection time; reusing it avoids DNS lookups on
// every refresh (and getaddrinfo has no timeout of its own).
struct DroboInfo {
    bool isDrobo = false; // true if the server answered on TCP 5000 with NASD XML
    std::wstring ip;      // resolved IPv4 address, empty when unknown
};

// Cache keyed by normalizeServer(server name). Only the worker thread touches
// it (detection and queries both run there), so no locking is needed.
using DroboCache = std::map<std::wstring, DroboInfo>;

// Removes cache entries whose server no longer appears in the share list.
void pruneDroboCache(DroboCache& cache, const std::vector<std::wstring>& shares);

// Extracts mTotalCapacityProtected / mUsedCapacityProtected /
// mFreeCapacityProtected from a NASD XML document (the text after the DRINASD*
// prefix). Returns true if all three fields were found and parsed as non-
// negative integers. Exposed separately so it can be unit tested without a
// network connection.
bool parseDroboXml(const std::string& xml, DroboCapacity& out);

// True if a received frame looks like Drobo status-daemon output: the DRINASD
// magic in the first seven bytes of the 16-byte header (current firmwares) or
// at the start of the body (older firmwares). Exposed separately so it can be
// unit tested without a network connection.
bool isDroboFrame(const std::string& header, const std::string& body);

// Queries a Drobo's real (non-thin-provisioned) capacity: resolves `server`,
// connects to TCP port 5000 with the given timeout, reads the pushed document,
// and parses it. Returns true if the server is a Drobo and its capacity was
// parsed; false otherwise (resolution/connection failure, timeout, or not a
// Drobo). If resolvedIp is non-null it receives the address that was connected
// to, on success.
bool queryDrobo(const std::wstring& server, int timeoutMs, DroboCapacity& out,
                std::wstring* resolvedIp = nullptr);

} // namespace rdsmon
