#pragma once
#include <cstdint>
#include <functional>

namespace eu {

// 64-bit random identifier. Every entity and asset reference in a saved scene uses one,
// so references survive save/load and duplication.
class UUID {
public:
    UUID();                         // random
    constexpr UUID(uint64_t v) : m_Value(v) {}
    constexpr operator uint64_t() const { return m_Value; }
    constexpr uint64_t Value() const { return m_Value; }
private:
    uint64_t m_Value;
};

} // namespace eu

namespace std {
template <> struct hash<eu::UUID> {
    size_t operator()(const eu::UUID& id) const noexcept { return std::hash<uint64_t>()(id.Value()); }
};
} // namespace std
