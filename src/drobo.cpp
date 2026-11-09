#include "drobo.h"

// winsock2.h must come before windows.h (Winsock types).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace rdsmon {
namespace {

// The Drobo status daemon port. Connecting makes the device push its NASD XML
// document; no command is sent. (The daemon also listens on TCP 5001 and UDP
// 5002, which are not needed here.)
constexpr int kStatusPort = 5000;

// The pushed document can be tens of kilobytes; reject anything larger as a
// protocol violation instead of allocating unbounded memory.
constexpr std::size_t kMaxBodyBytes = 1u << 20; // 1 MiB

// Lowercase copy, for case-insensitive share-name comparison (Windows share
// paths and hostnames are case-insensitive). Same approach as shares.cpp.
std::wstring toLower(const std::wstring& s) {
    std::wstring r(s);
    for (wchar_t& c : r) {
        c = static_cast<wchar_t>(::towlower(static_cast<wint_t>(c)));
    }
    return r;
}

// Narrow/wide conversions for the Winsock APIs, which are ANSI-only. Same
// approach as shares.cpp's toUtf8/toWide.
std::string toNarrow(const std::wstring& w) {
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
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(len), L'\0');
    if (len > 0) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    }
    return w;
}

// Reads the integer value of <tag>...</tag> from xml. Returns false if the tag
// is missing or its content is not a non-negative decimal integer (whitespace
// around the number is allowed).
bool readIntField(const std::string& xml, const char* tag, std::uint64_t& out) {
    const std::string open = std::string("<") + tag + ">";
    const std::string close = std::string("</") + tag + ">";
    const std::size_t b0 = xml.find(open);
    if (b0 == std::string::npos) {
        return false;
    }
    const std::size_t e = xml.find(close, b0 + open.size());
    if (e == std::string::npos) {
        return false;
    }
    // Trim surrounding whitespace.
    std::size_t b = b0 + open.size();
    std::size_t end = e;
    while (b < end && std::isspace(static_cast<unsigned char>(xml[b]))) ++b;
    while (end > b && std::isspace(static_cast<unsigned char>(xml[end - 1]))) --end;
    if (b >= end) {
        return false;
    }
    for (std::size_t i = b; i < end; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(xml[i]))) {
            return false;
        }
    }
    out = std::strtoull(xml.substr(b, end - b).c_str(), nullptr, 10);
    return true;
}

// Resolves host (a name or dotted IPv4 address) and connects to port with a
// timeout. Uses non-blocking connect + select() because blocking connect has
// no timeout of its own: a filtered port would otherwise stall the worker
// thread for ~21 seconds per share. Returns INVALID_SOCKET on any failure. If
// outIp is non-null it receives the resolved address as a dotted string.
SOCKET connectWithTimeout(const std::wstring& host, int port, int timeoutMs,
                          std::wstring* outIp) {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;      // Drobo status daemons are IPv4
    hints.ai_socktype = SOCK_STREAM;

    const std::string service = std::to_string(port);
    struct addrinfo* res = nullptr;
    // getaddrinfo is ANSI-only (there is no wide variant). The temporary from
    // toNarrow() lives until the end of this full expression.
    if (getaddrinfo(toNarrow(host).c_str(), service.c_str(), &hints, &res) != 0 || !res) {
        return INVALID_SOCKET;
    }

    SOCKET sock = socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }

    // Remember the resolved address before freeing the result list.
    if (outIp) {
        auto* addr = reinterpret_cast<sockaddr_in*>(res->ai_addr);
        char ipBuf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &addr->sin_addr, ipBuf, sizeof(ipBuf));
        *outIp = toWide(ipBuf);
    }

    u_long nonblocking = 1;
    ioctlsocket(sock, FIONBIO, &nonblocking);

    int rc = connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen));
    freeaddrinfo(res);

    if (rc == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            closesocket(sock);
            return INVALID_SOCKET;
        }
        // Wait for the connection to complete or fail. On Windows the first
        // select() argument is ignored, so 0 is correct.
        fd_set writeSet{};
        fd_set errorSet{};
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(sock, &writeSet);
        FD_SET(sock, &errorSet);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        rc = select(0, nullptr, &writeSet, &errorSet, &tv);
        if (rc <= 0) {
            closesocket(sock); // timed out or interrupted
            return INVALID_SOCKET;
        }
        int sockErr = 0;
        int len = sizeof(sockErr);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sockErr), &len);
        if (sockErr != 0) {
            closesocket(sock); // connection refused / reset
            return INVALID_SOCKET;
        }
    }

    // Back to blocking mode for the reads.
    nonblocking = 0;
    ioctlsocket(sock, FIONBIO, &nonblocking);
    return sock;
}

// Reads exactly count bytes from sock (the socket is in blocking mode with a
// receive timeout set by the caller). Returns false on timeout or disconnect.
bool recvAll(SOCKET sock, std::string& out, std::size_t count) {
    while (out.size() < count) {
        char buf[4096];
        const int want = static_cast<int>(std::min<std::size_t>(sizeof(buf), count - out.size()));
        const int n = recv(sock, buf, want, 0);
        if (n <= 0) {
            return false;
        }
        out.append(buf, static_cast<std::size_t>(n));
    }
    return true;
}

} // namespace

bool isDriveShare(const std::wstring& share) {
    // Same rule as the validation pipeline in shares.cpp: a single letter C-Z
    // (either case) followed by a colon.
    return share.size() == 2 && share[1] == L':' &&
           ((share[0] >= L'C' && share[0] <= L'Z') || (share[0] >= L'c' && share[0] <= L'z'));
}

std::wstring serverFromUnc(const std::wstring& share) {
    // Well-formed UNC path: exactly two leading backslashes, then the server,
    // then a single backslash, then the share (no further backslashes). Same
    // shape check as shares.cpp's malformed-share validation.
    if (share.size() < 5 || share[0] != L'\\' || share[1] != L'\\') {
        return {};
    }
    const std::size_t sep = share.find(L'\\', 2);
    if (sep == std::wstring::npos || sep + 1 >= share.size() ||
        share.find(L'\\', sep + 1) != std::wstring::npos) {
        return {};
    }
    const std::wstring server = share.substr(2, sep - 2);
    if (server.empty()) {
        return {};
    }
    return server;
}

std::wstring normalizeServer(const std::wstring& s) {
    return toLower(s);
}

void pruneDroboCache(DroboCache& cache, const std::vector<std::wstring>& shares) {
    // Collect the servers still present in the list (case-insensitive). The
    // DroboInfo values are unused; only key presence matters.
    DroboCache keep;
    for (const std::wstring& share : shares) {
        const std::wstring server = serverFromUnc(share);
        if (!server.empty()) {
            keep.emplace(normalizeServer(server), DroboInfo{});
        }
    }
    for (auto it = cache.begin(); it != cache.end();) {
        if (keep.find(it->first) == keep.end()) {
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
}

bool parseDroboXml(const std::string& xml, DroboCapacity& out) {
    return readIntField(xml, "mTotalCapacityProtected", out.totalBytes) &&
           readIntField(xml, "mUsedCapacityProtected", out.usedBytes) &&
           readIntField(xml, "mFreeCapacityProtected", out.freeBytes);
}

bool isDroboFrame(const std::string& header, const std::string& body) {
    // Current firmwares put the DRINASD magic in the first seven bytes of the
    // 16-byte header (older ones reportedly prefix the document itself). The
    // variants DRINASDa / DRINASD9 also start with DRINASD.
    return header.compare(0, 7, "DRINASD") == 0 || body.compare(0, 7, "DRINASD") == 0;
}

bool queryDrobo(const std::wstring& server, int timeoutMs, DroboCapacity& out,
                std::wstring* resolvedIp) {
    SOCKET sock = connectWithTimeout(server, kStatusPort, timeoutMs, resolvedIp);
    if (sock == INVALID_SOCKET) {
        return false;
    }

    // Bound the reads so a misbehaving peer cannot stall the worker thread.
    DWORD rcvTimeout = static_cast<DWORD>(timeoutMs);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout),
               sizeof(rcvTimeout));

    bool ok = false;
    std::string header;
    std::string body;
    // Framing (verified against a real Drobo, firmware 4.3.1): one frame per
    // connection — a 16-byte header whose last four bytes are the big-endian
    // length of the document that follows, then the device stops sending.
    // Unsigned arithmetic so a high bit in the first byte cannot overflow
    // (which would be UB).
    if (recvAll(sock, header, 16)) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(header.data());
        const std::uint32_t bodyLen = static_cast<std::uint32_t>(p[12]) << 24 |
                                      static_cast<std::uint32_t>(p[13]) << 16 |
                                      static_cast<std::uint32_t>(p[14]) << 8 |
                                      static_cast<std::uint32_t>(p[15]);
        if (bodyLen > 0 && bodyLen <= kMaxBodyBytes) {
            ok = recvAll(sock, body, static_cast<std::size_t>(bodyLen));
        }
    }
    closesocket(sock);

    // The DRINASD magic sits in the header on current firmwares; older ones
    // reportedly put it at the start of the document instead. Anything else on
    // port 5000 is not a Drobo.
    if (!ok || !isDroboFrame(header, body)) {
        return false;
    }

    return parseDroboXml(body, out);
}

} // namespace rdsmon
