#pragma once
#include <cstdint>
#include <string>

namespace nd {

struct NetAddress {
    uint32_t ip = 0;       // host byte order
    uint16_t port = 0;
    bool operator==(const NetAddress& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const NetAddress& o) const { return !(*this == o); }
    std::string ToString() const;
    static bool Resolve(const std::string& host, uint16_t port, NetAddress& out);
};

// Minimal non-blocking UDP socket (Winsock / BSD sockets).
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket() { Close(); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool Open(uint16_t port = 0);   // 0 = any port (clients)
    void Close();
    bool IsOpen() const { return m_Handle != kInvalid; }
    bool Send(const NetAddress& to, const void* data, int size);
    int Receive(NetAddress& from, void* buffer, int capacity);   // bytes, 0 if nothing

    static bool InitPlatform();

private:
#ifdef _WIN32
    using Handle = uintptr_t;
#else
    using Handle = int;
#endif
    static constexpr Handle kInvalid = (Handle)-1;
    Handle m_Handle = kInvalid;
};

} // namespace nd
