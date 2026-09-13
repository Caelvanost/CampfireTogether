#include "PCH.h"
#include "FireStateSync.h"

#include "SharedCampSync.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr auto kPlayerCellRefreshCooldown = std::chrono::seconds(5);
        constexpr auto kStateRebroadcastCooldown = std::chrono::seconds(10);
    }

    void FireStateSync::TrackCampfireReference(RE::TESObjectREFR* reference)
    {
        const auto key = GetKey(reference);
        if (key) {
            TrackReference(*key, reference);
        }
    }

    void FireStateSync::RefreshTrackedCampfires()
    {
        const auto now = std::chrono::steady_clock::now();

        bool scanPlayerCell = false;
        {
            std::scoped_lock lock(_mutex);
            if (_lastPlayerCellScan.time_since_epoch().count() == 0 ||
                now - _lastPlayerCellScan >= kPlayerCellRefreshCooldown) {
                _lastPlayerCellScan = now;
                scanPlayerCell = true;
            }
        }

        if (scanPlayerCell) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            if (cell && cell->IsAttached()) {
                OnCellFullyLoaded(cell);
            }
        }

        // The current writer is authoritative for passive timer progression.
        // A different player may become writer after a real local interaction
        // (for example Replenish Fuel), but non-writers must never echo their
        // local timer estimate back onto the network.
        static auto lastStateRebroadcast = std::chrono::steady_clock::time_point{};
        if (lastStateRebroadcast.time_since_epoch().count() != 0 &&
            now - lastStateRebroadcast < kStateRebroadcastCooldown) {
            return;
        }
        lastStateRebroadcast = now;

        const auto localNodeID = SharedCampSync::GetSingleton().GetLocalNodeID();
        std::vector<State> retryStates;
        {
            std::scoped_lock lock(_mutex);
            retryStates.reserve(_tracked.size());
            for (const auto& [key, handle] : _tracked) {
                auto reference = handle.get();
                if (!reference || reference->IsMarkedForDeletion()) {
                    continue;
                }
                if (const auto stateIt = _states.find(key);
                    stateIt != _states.end() && stateIt->second.writerNodeID == localNodeID) {
                    retryStates.push_back(stateIt->second);
                }
            }
        }

        for (const auto& state : retryStates) {
            if (!SharedCampSync::GetSingleton().IsCampActive(
                    state.key.originNodeID,
                    state.key.objectID)) {
                continue;
            }

            SKSE::log::debug(
                "CFT FIRE RETRY object={:016X}:{} rev={} writer={:016X} remaining={:.2f}",
                state.key.originNodeID,
                state.key.objectID,
                state.revision,
                state.writerNodeID,
                state.remainingHours);
            BroadcastState(state);
        }
    }

    bool FireStateSync::CanReportObserved(RE::TESObjectREFR* reference) const
    {
        const auto key = GetKey(reference);
        if (!key) {
            return false;
        }

        {
            std::scoped_lock lock(_mutex);
            if (_states.contains(*key)) {
                return true;
            }
        }

        // Prevent both clients from independently publishing revision 1 from
        // their local physical copies. The entity creator seeds the state once;
        // after that, every client may publish later revisions after interaction.
        return key->originNodeID == SharedCampSync::GetSingleton().GetLocalNodeID();
    }
}
