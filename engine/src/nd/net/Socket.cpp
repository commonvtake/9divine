#include "nd/net/Socket.h"
#include "nd/core/Log.h"
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

namespace nd {

bool UdpSocket::InitPlatform() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
        done = true;
    }
#endif
    return true;
}

std::string NetAddress::ToString() const {
    return std::to_string((ip >> 24) & 255) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "." +
           std::to_string(ip & 255) + ":" + std::to_string(port);
}

bool NetAddress::Resolve(const std::string& host, uint16_t port, NetAddress& out) {
    UdpSocket::InitPlatform();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    out.ip = ntohl(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
    out.port = port;
    freeaddrinfo(res);
    return true;
}

bool UdpSocket::Open(uint16_t port) {
    if (!InitPlatform()) return false;
    Close();
    m_Handle = (Handle)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_Handle == kInvalid) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(m_Handle, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ND_ERROR("Could not bind UDP port {}", port);
        Close();
        return false;
    }
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(m_Handle, FIONBIO, &nonBlocking);
#else
    fcntl(m_Handle, F_SETFL, fcntl(m_Handle, F_GETFL, 0) | O_NONBLOCK);
#endif
    return true;
}

void UdpSocket::Close() {
    if (m_Handle == kInvalid) return;
#ifdef _WIN32
    closesocket(m_Handle);
#else
    close(m_Handle);
#endif
    m_Handle = kInvalid;
}

bool UdpSocket::Send(const NetAddress& to, const void* data, int size) {
    if (!IsOpen()) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port = htons(to.port);
    return sendto(m_Handle, (const char*)data, size, 0, (sockaddr*)&addr, sizeof(addr)) == size;
}

int UdpSocket::Receive(NetAddress& from, void* buffer, int capacity) {
    if (!IsOpen()) return 0;
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int n = (int)recvfrom(m_Handle, (char*)buffer, capacity, 0, (sockaddr*)&addr, &len);
    if (n <= 0) return 0;
    from.ip = ntohl(addr.sin_addr.s_addr);
    from.port = ntohs(addr.sin_port);
    return n;
}

} // namespace nd
