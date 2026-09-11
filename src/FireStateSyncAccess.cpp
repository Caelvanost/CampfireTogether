#include "PCH.h"
#include "FireStateSync.h"

#include "SharedCampSync.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr auto kPlayerCellRefreshCooldown = std::chrono::seconds(5);
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
        {
            std::scoped_lock lock(_mutex);
            if (_lastPlayerCellScan.time_since_epoch().count() != 0 &&
                now - _lastPlayerCellScan < kPlayerCellRefreshCooldown) {
                return;
            }
            _lastPlayerCellScan = now;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        if (cell && cell->IsAttached()) {
            OnCellFullyLoaded(cell);
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
