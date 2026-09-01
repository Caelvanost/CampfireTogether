#include "PCH.h"
#include "CellTracker.h"

#include "CampfireSync.h"

namespace CampfireTogether::CellTracker
{
    namespace
    {
        void ProcessLoadedCell(RE::TESObjectCELL* cell)
        {
            if (!cell) {
                return;
            }

            auto& sync = CampfireSync::GetSingleton();
            sync.OnCellFullyLoaded(cell);
            if (cell->IsExteriorCell()) {
                sync.RefreshRemoteExteriorCell(cell);
            }
        }

        void RetryCellAfterLoad(RE::FormID cellID)
        {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks || cellID == 0) {
                return;
            }

            tasks->AddTask([cellID]() {
                auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellID);
                if (!cell) {
                    SKSE::log::debug("CFT CELL deferred retry unresolved cell={:08X}", cellID);
                    return;
                }

                SKSE::log::debug("CFT CELL deferred retry cell={:08X}", cellID);
                ProcessLoadedCell(cell);
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
                    const auto cellID = event->cell->GetFormID();
                    ProcessLoadedCell(event->cell);
                    RetryCellAfterLoad(cellID);
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
        SKSE::log::info("CFT CELL TRACKER READY event=TESCellFullyLoadedEvent exteriorGridRetry=1 deferredRetry=1");
        return true;
    }
}
