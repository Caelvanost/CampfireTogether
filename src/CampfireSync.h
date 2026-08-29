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
        void OnPeerAvailable(STRPM::ConnectionID connectionID);
        void OnPeerUnavailable(STRPM::ConnectionID connectionID);
        void OnAllPeersUnavailable();
        void OnCellFullyLoaded(RE::TESObjectCELL* cell);
        void BroadcastSnapshot();

        [[nodiscard]] bool IsRemoteCampObject(RE::TESObjectREFR* reference) const;

        void SavePersistentState(SKSE::SerializationInterface* serialization) const;
        void LoadPersistentState(SKSE::SerializationInterface* serialization);
        void ClearPersistentState();

        void ResetRemoteState();
        void Reset();

    private:
        struct LocalPlacement
        {
            std::uint64_t eventID{ 0 };
            std::string pluginName;
            RE::FormID localFormID{ 0 };
            std::string cellPluginName;
            RE::FormID cellLocalFormID{ 0 };
            float x{ 0.0f };
            float y{ 0.0f };
            float z{ 0.0f };
            float angleX{ 0.0f };
            float angleY{ 0.0f };
            float angleZ{ 0.0f };
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

        struct RemotePlacement
        {
            std::string pluginName;
            RE::FormID localFormID{ 0 };
            std::string cellPluginName;
            RE::FormID cellLocalFormID{ 0 };
            float x{ 0.0f };
            float y{ 0.0f };
            float z{ 0.0f };
            float angleX{ 0.0f };
            float angleY{ 0.0f };
            float angleZ{ 0.0f };
            bool isTent{ false };
        };

        struct RemoteMirror
        {
            RE::ObjectRefHandle handle{};
            RE::FormID runtimeBaseFormID{ 0 };
            std::string pluginName;
            RE::FormID localFormID{ 0 };
            std::string cellPluginName;
            RE::FormID cellLocalFormID{ 0 };
            float x{ 0.0f };
            float y{ 0.0f };
            float z{ 0.0f };
            bool isTent{ false };
        };

        struct SuppressedRemoval
        {
            RE::FormID baseFormID{ 0 };
            float x{ 0.0f };
            float y{ 0.0f };
            float z{ 0.0f };
            bool isTent{ false };
            std::chrono::steady_clock::time_point expiresAt{};
        };

        struct RemoteSnapshotState
        {
            std::uint64_t snapshotID{ 0 };
            std::unordered_set<std::uint64_t> baselineEvents;
            std::unordered_set<std::uint64_t> seenEvents;
        };

        [[nodiscard]] std::optional<LocalPlacement> TakeLocalPlacement(
            std::string_view pluginName,
            RE::FormID localFormID,
            float x,
            float y,
            float z,
            bool isTent);
        [[nodiscard]] bool ConsumeSuppressedRemoval(RE::FormID baseFormID, float x, float y, float z, bool isTent);
        void MarkSuppressedRemoval(const RemoteMirror& mirror);

        void SendSnapshot(std::optional<STRPM::ConnectionID> target);
        void BeginRemoteSnapshot(STRPM::ConnectionID sender, std::uint64_t snapshotID);
        void EndRemoteSnapshot(STRPM::ConnectionID sender, std::uint64_t snapshotID);
        void StoreRemotePlacement(STRPM::ConnectionID sender, const Protocol::Packet& packet);
        void RemoveRemote(STRPM::ConnectionID sender, const Protocol::Packet& packet);
        void TryMaterializeRemote(const RemoteKey& key);
        void TeardownMirror(const RemoteMirror& mirror);
        static bool DispatchTakeDown(RE::ObjectRefHandle handle, const char* scriptName);
        static void DeleteMirror(RE::ObjectRefHandle handle);

        std::atomic<std::uint64_t> _nextEventID{ 1 };
        std::atomic<std::uint64_t> _nextSnapshotID{ 1 };
        mutable std::mutex _mutex;
        std::deque<LocalPlacement> _localPlacements;
        std::unordered_map<RemoteKey, RemotePlacement, RemoteKeyHash> _remotePlacements;
        std::unordered_map<RemoteKey, RemoteMirror, RemoteKeyHash> _remoteMirrors;
        std::unordered_map<STRPM::ConnectionID, RemoteSnapshotState> _remoteSnapshots;
        std::deque<SuppressedRemoval> _suppressedRemovals;
    };
}
