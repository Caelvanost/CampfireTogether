#include "PCH.h"
#include "CellTracker.h"

#include "FireStateSync.h"
#include "FireStateTransport.h"
#include "SharedCampSync.h"
#include "STRPMClient.h"

namespace CampfireTogether::CellTracker
{
    namespace
    {
        void QueueCellAfterLoad(RE::FormID cellID)
        {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks || cellID == 0) {
                return;
            }

            tasks->AddTask([cellID]() {
                auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellID);
                if (!cell) {
                    SKSE::log::debug("CFT CELL deferred unresolved cell={:08X}", cellID);
                    return;
                }

                STRPMClient::GetSingleton().ProbeStateExchange();
                FireStateTransport::GetSingleton().ProbeStateExchange();

                SKSE::log::debug("CFT CELL deferred process cell={:08X}", cellID);
                SharedCampSync::GetSingleton().OnCellFullyLoaded(cell);

                // Fire-state discovery uses ForEachReference. Keep that scan limited
                // to the player's actual cell so fast travel cannot reintroduce the
                // v0.2.4 streaming regression where every loaded exterior cell was scanned.
                if (auto* player = RE::PlayerCharacter::GetSingleton();
                    player && player->GetParentCell() == cell) {
                    FireStateSync::GetSingleton().OnCellFullyLoaded(cell);
                }
            });
        }

        class CellFullyLoadedSink final : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
        {
        public:
            static CellFullyLoadedSink& GetSingleton()
            {
                static CellFullyLoadedSink instance;
                return instance;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESCellFullyLoadedEvent* event,
                RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
            {
                if (event && event->cell) {
                    QueueCellAfterLoad(event->cell->GetFormID());
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        bool g_registered = false;
    }

    bool Register()
    {
        if (g_registered) {
            return true;
        }

        auto* events = RE::ScriptEventSourceHolder::GetSingleton();
        if (!events) {
            SKSE::log::warn("CFT CELL TRACKER unavailable: ScriptEventSourceHolder missing");
            return false;
        }

        events->AddEventSink<RE::TESCellFullyLoadedEvent>(&CellFullyLoadedSink::GetSingleton());
        g_registered = true;
        SKSE::log::info("CFT CELL TRACKER READY event=TESCellFullyLoadedEvent sharedRegistry=1 fireState=1 playerCellScanOnly=1 bootstrapProbe=1");
        return true;
    }
}
