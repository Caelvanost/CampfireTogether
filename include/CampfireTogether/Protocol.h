#pragma once

#include <cstdint>
#include <type_traits>

namespace CampfireTogether::Protocol
{
    inline constexpr std::uint32_t kMagic = 0x31544643;  // "CFT1"
    inline constexpr std::uint16_t kVersion = 1;

    enum class PacketType : std::uint8_t
    {
        kPlace = 1,
        kRemove = 2
    };

    enum PacketFlags : std::uint8_t
    {
        kNone = 0,
        kTent = 1u << 0
    };

#pragma pack(push, 1)
    struct Packet
    {
        std::uint32_t magic{ kMagic };
        std::uint16_t version{ kVersion };
        PacketType type{ PacketType::kPlace };
        std::uint8_t flags{ kNone };
        std::uint64_t eventID{ 0 };
        std::uint32_t baseFormID{ 0 };
        float positionX{ 0.0f };
        float positionY{ 0.0f };
        float positionZ{ 0.0f };
        float angleX{ 0.0f };
        float angleY{ 0.0f };
        float angleZ{ 0.0f };
    };
#pragma pack(pop)

    static_assert(sizeof(Packet) == 44);
    static_assert(std::is_trivially_copyable_v<Packet>);

    [[nodiscard]] inline bool IsValid(const Packet& packet) noexcept
    {
        return packet.magic == kMagic &&
               packet.version == kVersion &&
               (packet.type == PacketType::kPlace ||
                packet.type == PacketType::kRemove) &&
               packet.baseFormID != 0;
    }
}
