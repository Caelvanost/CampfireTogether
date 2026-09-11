#pragma once

#include "CampfireTogether/FireStateProtocol.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace CampfireTogether
{
    class FireStateSync
    {
    public:
        static FireStateSync& GetSingleton();

        void OnCellFullyLoaded(RE::TESObjectCELL* cell);
        void RefreshTrackedCampfires();
        void TrackCampfireReference(RE::TESObjectREFR* reference);
        [[nodiscard]] bool CanReportObserved(RE::TESObjectREFR* reference) const;
        void ReportObserved(
            RE::TESObjectREFR* reference,
            std::int32_t stage,
            std::int32_t size,
            float remainingHours,
            RE::TESForm* fuelLit,
            RE::TESForm* fuelUnlit,
            RE::TESForm* light);

        [[nodiscard]] std::int32_t GetTrackedCount() const;
        [[nodiscard]] RE::TESObjectREFR* GetTracked(std::int32_t index) const;
        [[nodiscard]] bool NeedsApply(RE::TESObjectREFR* reference) const;
        [[nodiscard]] std::int32_t GetDesiredStage(RE::TESObjectREFR* reference) const;
        [[nodiscard]] std::int32_t GetDesiredSize(RE::TESObjectREFR* reference) const;
        [[nodiscard]] float GetDesiredRemainingHours(RE::TESObjectREFR* reference) const;
        [[nodiscard]] RE::TESForm* GetDesiredFuelLit(RE::TESObjectREFR* reference) const;
        [[nodiscard]] RE::TESForm* GetDesiredFuelUnlit(RE::TESObjectREFR* reference) const;
        [[nodiscard]] RE::TESForm* GetDesiredLight(RE::TESObjectREFR* reference) const;
        void AcknowledgeApplied(RE::TESObjectREFR* reference);

        void HandleRemote(STRPM::ConnectionID sender, const FireStateProtocol::Packet& packet);
        void SendSnapshot(std::optional<STRPM::ConnectionID> target);
        void ClearRuntime();
        void ClearAll();

    private:
        struct Key
        {
            std::uint64_t originNodeID{ 0 };
            std::uint64_t objectID{ 0 };
            bool operator==(const Key&) const = default;
        };

        struct KeyHash
        {
            std::size_t operator()(const Key& key) const noexcept
            {
                const auto h1 = std::hash<std::uint64_t>{}(key.originNodeID);
                const auto h2 = std::hash<std::uint64_t>{}(key.objectID);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
            }
        };

        struct FormIdentity
        {
            std::string pluginName;
            RE::FormID localFormID{ 0 };
            bool operator==(const FormIdentity&) const = default;
        };

        struct State
        {
            Key key{};
            std::uint64_t revision{ 0 };
            std::uint64_t writerNodeID{ 0 };
            std::int32_t stage{ 0 };
            std::int32_t size{ 0 };
            float remainingHours{ 0.0f };
            FormIdentity fuelLit{};
            FormIdentity fuelUnlit{};
            FormIdentity light{};
        };

        struct SnapshotReceiveState
        {
            std::uint64_t snapshotID{ 0 };
            std::size_t seen{ 0 };
        };

        [[nodiscard]] std::optional<Key> GetKey(RE::TESObjectREFR* reference) const;
        [[nodiscard]] std::optional<FormIdentity> DescribeForm(RE::TESForm* form) const;
        [[nodiscard]] RE::TESForm* ResolveForm(const FormIdentity& identity) const;
        [[nodiscard]] bool MeaningfullyDifferent(const State& current, const State& observed) const;
        [[nodiscard]] bool Merge(const State& incoming, bool fromNetwork);
        [[nodiscard]] std::optional<State> GetStateForReference(RE::TESObjectREFR* reference) const;
        void BroadcastState(const State& state);
        void TrackReference(const Key& key, RE::TESObjectREFR* reference);
        void PruneInvalidTracked();

        mutable std::mutex _mutex;
        std::unordered_map<Key, State, KeyHash> _states;
        std::unordered_map<Key, RE::ObjectRefHandle, KeyHash> _tracked;
        std::unordered_map<Key, std::uint64_t, KeyHash> _appliedRevision;
        std::unordered_map<STRPM::ConnectionID, SnapshotReceiveState> _remoteSnapshots;
        std::chrono::steady_clock::time_point _lastPlayerCellScan{};
        std::atomic<std::uint64_t> _nextSnapshotID{ 1 };
    };
}
