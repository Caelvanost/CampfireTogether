#include "PCH.h"
#include "CellTracker.h"

#include "CampfireSync.h"

namespace CampfireTogether::CellTracker
{
    namespace
    {
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
                    CampfireSync::GetSingleton().OnCellFullyLoaded(event->cell);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        class PlayerCellSink final : public RE::BSTEventSink<RE::BGSActorCellEvent>
        {
        public:
            static PlayerCellSink& GetSingleton()
            {
                static PlayerCellSink instance;
                return instance;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::BGSActorCellEvent* event,
                RE::BSTEventSource<RE::BGSActorCellEvent>*) override
            {
                if (!event || event->flags != RE::BGSActorCellEvent::CellFlag::kEnter || event->cellID == 0) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(event->cellID);
                if (!cell) {
                    SKSE::log::debug("CFT PLAYER CELL enter unresolved cell={:08X}", event->cellID);
                    return RE::BSEventNotifyControl::kContinue;
                }

                SKSE::log::info("CFT PLAYER CELL enter cell={:08X}", event->cellID);
                CampfireSync::GetSingleton().OnCellFullyLoaded(cell);
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
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!events || !player) {
            SKSE::log::warn(
                "CFT CELL TRACKER unavailable: eventSource={} player={}",
                events ? 1 : 0,
                player ? 1 : 0);
            return false;
        }

        events->AddEventSink<RE::TESCellFullyLoadedEvent>(&CellFullyLoadedSink::GetSingleton());
        player->AsBGSActorCellEventSource()->AddEventSink(&PlayerCellSink::GetSingleton());
        g_registered = true;
        SKSE::log::info("CFT CELL TRACKER READY events=TESCellFullyLoadedEvent,BGSActorCellEvent");
        return true;
    }
}
