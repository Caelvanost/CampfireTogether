#pragma once

#include <cstdint>
#include <type_traits>

namespace CampfireTogether::Protocol
{
    inline constexpr std::uint32_t kMagic = 0x31544643;  // "CFT1"
    inline constexpr std::uint16_t kVersion = 3;
    inline constexpr std::uint32_t kPluginNameCapacity = 260;

    enum class PacketType : std::uint8_t
    {
        kPlace = 1,
        kRemove = 2,
        kSnapshotRequest = 3,
        kSnapshotBegin = 4,
        kSnapshotEnd = 5
    };

    enum PacketFlags : std::uint8_t
    {
        kNone = 0,
        kTent = 1u << 0,
        kSnapshot = 1u << 1
    };

#pragma pack(push, 1)
    struct Packet
    {
        std::uint32_t magic{ kMagic };
        std::uint16_t version{ kVersion };
        PacketType type{ PacketType::kPlace };
        std::uint8_t flags{ kNone };
        std::uint64_t eventID{ 0 };
        std::uint64_t snapshotID{ 0 };
        std::uint32_t baseLocalFormID{ 0 };
        char basePluginName[kPluginNameCapacity]{};
        std::uint32_t cellLocalFormID{ 0 };
        char cellPluginName[kPluginNameCapacity]{};
        float positionX{ 0.0f };
        float positionY{ 0.0f };
        float positionZ{ 0.0f };
        float angleX{ 0.0f };
        float angleY{ 0.0f };
        float angleZ{ 0.0f };
    };
#pragma pack(pop)

    static_assert(sizeof(Packet) == 576);
    static_assert(std::is_trivially_copyable_v<Packet>);

    [[nodiscard]] inline bool IsObjectPacket(const Packet& packet) noexcept
    {
        return packet.type == PacketType::kPlace || packet.type == PacketType::kRemove;
    }

    [[nodiscard]] inline bool IsControlPacket(const Packet& packet) noexcept
    {
        return packet.type == PacketType::kSnapshotRequest ||
               packet.type == PacketType::kSnapshotBegin ||
               packet.type == PacketType::kSnapshotEnd;
    }

    [[nodiscard]] inline bool HasValidIdentity(
        std::uint32_t localFormID,
        const char (&pluginName)[kPluginNameCapacity]) noexcept
    {
        return localFormID != 0 &&
               pluginName[0] != '\0' &&
               pluginName[kPluginNameCapacity - 1] == '\0';
    }

    [[nodiscard]] inline bool IsValid(const Packet& packet) noexcept
    {
        if (packet.magic != kMagic || packet.version != kVersion) {
            return false;
        }

        if (IsObjectPacket(packet)) {
            if (!HasValidIdentity(packet.baseLocalFormID, packet.basePluginName) ||
                !HasValidIdentity(packet.cellLocalFormID, packet.cellPluginName)) {
                return false;
            }

            if (packet.type == PacketType::kPlace && packet.eventID == 0) {
                return false;
            }

            if ((packet.flags & kSnapshot) != 0 &&
                (packet.snapshotID == 0 || packet.eventID == 0)) {
                return false;
            }

            return true;
        }

        if (IsControlPacket(packet)) {
            return packet.snapshotID != 0;
        }

        return false;
    }
}
