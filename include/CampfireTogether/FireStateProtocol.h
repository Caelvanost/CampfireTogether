#pragma once

#include <cstdint>
#include <type_traits>

namespace CampfireTogether::FireStateProtocol
{
    inline constexpr std::uint32_t kMagic = 0x46544643;  // "CFTF"
    inline constexpr std::uint16_t kVersion = 1;
    inline constexpr std::uint32_t kPluginNameCapacity = 260;

    enum class PacketType : std::uint8_t
    {
        kState = 1,
        kSnapshotRequest = 2,
        kSnapshotBegin = 3,
        kSnapshotEnd = 4
    };

    enum PacketFlags : std::uint8_t
    {
        kNone = 0,
        kSnapshot = 1u << 0
    };

#pragma pack(push, 1)
    struct FormIdentity
    {
        std::uint32_t localFormID{ 0 };
        char pluginName[kPluginNameCapacity]{};
    };

    struct Packet
    {
        std::uint32_t magic{ kMagic };
        std::uint16_t version{ kVersion };
        PacketType type{ PacketType::kState };
        std::uint8_t flags{ kNone };
        std::uint64_t originNodeID{ 0 };
        std::uint64_t objectID{ 0 };
        std::uint64_t revision{ 0 };
        std::uint64_t writerNodeID{ 0 };
        std::uint64_t snapshotID{ 0 };
        std::uint8_t stage{ 0 };
        std::uint8_t size{ 0 };
        std::uint16_t reserved{ 0 };
        float remainingHours{ 0.0f };
        FormIdentity fuelLit{};
        FormIdentity fuelUnlit{};
        FormIdentity light{};
    };
#pragma pack(pop)

    static_assert(sizeof(FormIdentity) == 264);
    static_assert(sizeof(Packet) == 844);
    static_assert(std::is_trivially_copyable_v<Packet>);

    [[nodiscard]] inline bool IsStatePacket(const Packet& packet) noexcept
    {
        return packet.type == PacketType::kState;
    }

    [[nodiscard]] inline bool IsControlPacket(const Packet& packet) noexcept
    {
        return packet.type == PacketType::kSnapshotRequest ||
               packet.type == PacketType::kSnapshotBegin ||
               packet.type == PacketType::kSnapshotEnd;
    }

    [[nodiscard]] inline bool IsValid(const Packet& packet) noexcept
    {
        if (packet.magic != kMagic || packet.version != kVersion) {
            return false;
        }

        if (IsStatePacket(packet)) {
            if (packet.originNodeID == 0 || packet.objectID == 0 ||
                packet.revision == 0 || packet.writerNodeID == 0 ||
                packet.stage > 5 || packet.size > 4) {
                return false;
            }
            if ((packet.flags & kSnapshot) != 0 && packet.snapshotID == 0) {
                return false;
            }
            return true;
        }

        return IsControlPacket(packet) && packet.snapshotID != 0;
    }
}
