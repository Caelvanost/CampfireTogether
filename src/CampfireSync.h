#pragma once

#include "CampfireTogether/Protocol.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace CampfireTogether
{
    class CampfireSync
    {
    public:
        static CampfireSync& GetSingleton();

        void OnLocalPlaced(
            RE::TESObjectREFR* placedRef,
            float positionX,
            float positionY,
            float positionZ,
            float angleX,
            float angleY,
            float angleZ,
            bool isTent);

        void OnLocalRemoved(
            RE::TESForm* baseForm,
            float positionX,
            float positionY,
            float positionZ,
            float angleX,
            float angleY,
            float angleZ,
            bool isTent);

        void HandleRemote(STRPM::ConnectionID sender, const Protocol::Packet& packet);
        void Reset();

    private:
        struct LocalPlacement
        {
            std::uint64_t eventID{ 0 };
            RE::FormID baseFormID{ 0 };
            float x{ 0.0f };
            float y{ 0.0f };
            float z{ 0.0f };
            bool isTent{ false };
        };

        struct RemoteKey
        {
            STRPM::ConnectionID sender{ 0 };
            std::uint64_t eventID{ 0 };

            bool operator==(const RemoteKey&) const = default;
        };

        struct RemoteKeyHash
        {
            std::size_t operator()(const RemoteKey& key) const noexcept
            {
                const auto h1 = std::hash<std::uint64_t>{}(key.sender);
                const auto h2 = std::hash<std::uint64_t>{}(key.eventID);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
            }
        };

        [[nodiscard]] std::uint64_t MatchLocalRemoval(RE::FormID baseFormID, float x, float y, float z, bool isTent);
        void SpawnRemote(STRPM::ConnectionID sender, const Protocol::Packet& packet);
        void RemoveRemote(STRPM::ConnectionID sender, const Protocol::Packet& packet);
        static void DeleteMirror(RE::ObjectRefHandle handle);

        std::atomic<std::uint64_t> _nextEventID{ 1 };
        std::mutex _mutex;
        std::deque<LocalPlacement> _localPlacements;
        std::unordered_map<RemoteKey, RE::ObjectRefHandle, RemoteKeyHash> _remoteMirrors;
    };
}
