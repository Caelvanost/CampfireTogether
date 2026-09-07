#pragma once

#include "CampfireTogether/Protocol.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace CampfireTogether
{
    class SharedCampSync
    {
    public:
        static SharedCampSync& GetSingleton();

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

        // Compatibility stubs for the short-lived v0.2.8 Papyrus materialization bridge.
        // v0.3.0 materializes directly from actual TESCellFullyLoadedEvent cells.
        [[nodiscard]] bool IsRemoteMaterializationRequestValid(std::uint32_t) const { return false; }
        [[nodiscard]] float GetRemoteMaterializationX(std::uint32_t) const { return 0.0f; }
        [[nodiscard]] float GetRemoteMaterializationY(std::uint32_t) const { return 0.0f; }
        [[nodiscard]] float GetRemoteMaterializationZ(std::uint32_t) const { return 0.0f; }
        [[nodiscard]] float GetRemoteMaterializationAngleX(std::uint32_t) const { return 0.0f; }
        [[nodiscard]] float GetRemoteMaterializationAngleY(std::uint32_t) const { return 0.0f; }
        [[nodiscard]] float GetRemoteMaterializationAngleZ(std::uint32_t) const { return 0.0f; }
        void CompleteRemoteMaterialization(std::uint32_t, RE::TESObjectREFR* reference);
        void FailRemoteMaterialization(std::uint32_t) {}

        void SavePersistentState(SKSE::SerializationInterface* serialization);
        void LoadPersistentState(SKSE::SerializationInterface* serialization);
        void ClearPersistentState();

        void ResetRuntimeState();
        void Reset();

    private:
        struct CampID
        {
            std::uint64_t originNodeID{ 0 };
            std::uint64_t objectID{ 0 };

            bool operator==(const CampID&) const = default;
        };

        struct CampIDHash
        {
            std::size_t operator()(const CampID& id) const noexcept
            {
                const auto h1 = std::hash<std::uint64_t>{}(id.originNodeID);
                const auto h2 = std::hash<std::uint64_t>{}(id.objectID);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
            }
        };

        struct CampRecord
        {
            CampID id{};
            std::uint64_t revision{ 0 };
            std::uint64_t writerNodeID{ 0 };
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
            bool deleted{ false };
        };

        struct Mirror
        {
            RE::ObjectRefHandle handle{};
            RE::FormID runtimeBaseFormID{ 0 };
            std::string pluginName;
            RE::FormID localFormID{ 0 };
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

        struct SnapshotReceiveState
        {
            std::uint64_t snapshotID{ 0 };
            std::size_t seen{ 0 };
        };

        [[nodiscard]] std::uint64_t EnsureLocalNodeID();
        [[nodiscard]] CampID AllocateCampID();
        [[nodiscard]] bool MergeRecord(const CampRecord& incoming, bool fromNetwork);
        [[nodiscard]] std::optional<CampID> FindActiveCamp(
            std::string_view pluginName,
            RE::FormID localFormID,
            float x,
            float y,
            float z,
            bool isTent) const;

        void SendSnapshot(std::optional<STRPM::ConnectionID> target);
        void BroadcastRecord(const CampRecord& record, std::uint64_t snapshotID = 0);
        void ApplyRuntimeState(const CampID& id);
        void MaterializeIfPossible(const CampID& id, const CampRecord& record);
        [[nodiscard]] RE::TESObjectCELL* FindLoadedCellForRecord(const CampRecord& record) const;
        [[nodiscard]] RE::TESObjectREFR* FindAnchor(RE::TESObjectCELL* cell) const;

        [[nodiscard]] bool ConsumeSuppressedRemoval(RE::FormID baseFormID, float x, float y, float z, bool isTent);
        void MarkSuppressedRemoval(const Mirror& mirror);
        void TeardownMirror(const Mirror& mirror);
        static bool DispatchTakeDown(RE::ObjectRefHandle handle, const char* scriptName);
        static void DeleteMirror(RE::ObjectRefHandle handle);

        mutable std::mutex _mutex;
        std::unordered_map<CampID, CampRecord, CampIDHash> _sharedCamps;
        std::unordered_map<CampID, Mirror, CampIDHash> _mirrors;
        std::unordered_map<STRPM::ConnectionID, SnapshotReceiveState> _remoteSnapshots;
        std::unordered_set<RE::FormID> _loadedExteriorCells;
        std::deque<SuppressedRemoval> _suppressedRemovals;

        std::uint64_t _localNodeID{ 0 };
        std::uint64_t _nextObjectID{ 1 };
        std::atomic<std::uint64_t> _nextSnapshotID{ 1 };
    };
}
