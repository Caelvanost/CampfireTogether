#include "PCH.h"
#include "LocalBuildIntent.h"

namespace CampfireTogether::LocalBuildIntent
{
    namespace
    {
        constexpr auto kIntentLifetime = std::chrono::milliseconds(2000);

        std::atomic_bool g_registered{ false };
        std::mutex g_mutex;
        std::chrono::steady_clock::time_point g_expiresAt{};
        std::string g_claimedPowerTag;

        bool IsShoutAction(const RE::ButtonEvent& button)
        {
            const auto& userEvent = button.QUserEvent();
            if (const auto* userEvents = RE::UserEvents::GetSingleton(); userEvents && userEvent == userEvents->shout) {
                return true;
            }

            return userEvent == "Shout"sv;
        }

        void Arm(const RE::ButtonEvent& button)
        {
            {
                std::scoped_lock lock(g_mutex);
                g_expiresAt = std::chrono::steady_clock::now() + kIntentLifetime;
                g_claimedPowerTag.clear();
            }

            SKSE::log::info(
                "CFT LOCAL POWER INTENT armed action=Shout device={} id={} window_ms=2000",
                static_cast<unsigned>(button.GetDevice()),
                button.GetIDCode());
        }

        [[nodiscard]] bool HasLiveIntentLocked(std::chrono::steady_clock::time_point now)
        {
            if (g_expiresAt == std::chrono::steady_clock::time_point{} || g_expiresAt < now) {
                g_expiresAt = {};
                g_claimedPowerTag.clear();
                return false;
            }
            return true;
        }

        class InputSink final :
            public RE::BSTEventSink<RE::InputEvent*>
        {
        public:
            static InputSink& GetSingleton()
            {
                static InputSink instance;
                return instance;
            }

            RE::BSEventNotifyControl ProcessEvent(
                RE::InputEvent* const* events,
                RE::BSTEventSource<RE::InputEvent*>*) override
            {
                if (!events) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                for (auto* event = *events; event; event = event->next) {
                    if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
                        continue;
                    }

                    auto* button = event->AsButtonEvent();
                    if (!button || !button->IsDown() || !IsShoutAction(*button)) {
                        continue;
                    }

                    Arm(*button);
                    break;
                }

                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void RegisterInputSink()
    {
        if (g_registered.load(std::memory_order_acquire)) {
            return;
        }

        auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (!inputManager) {
            SKSE::log::warn("CFT LOCAL POWER INTENT input sink unavailable: BSInputDeviceManager is null");
            return;
        }

        if (g_registered.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        inputManager->AddEventSink(std::addressof(InputSink::GetSingleton()));
        SKSE::log::info("CFT LOCAL POWER INTENT input sink READY action=Shout window_ms=2000");
    }

    void Reset()
    {
        std::scoped_lock lock(g_mutex);
        g_expiresAt = {};
        g_claimedPowerTag.clear();
    }

    bool ConsumePower(std::string_view powerTag, RE::Actor* caster)
    {
        if (powerTag.empty()) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (caster && player && caster != player) {
            SKSE::log::info(
                "CFT REMOTE POWER suppressed tag={} caster={:08X} reason=remote-caster",
                powerTag,
                caster->GetFormID());
            return false;
        }

        std::scoped_lock lock(g_mutex);
        const auto now = std::chrono::steady_clock::now();
        if (!HasLiveIntentLocked(now)) {
            SKSE::log::info(
                "CFT REMOTE POWER suppressed tag={} caster={:08X} reason=no-local-intent",
                powerTag,
                caster ? caster->GetFormID() : 0);
            return false;
        }

        if (g_claimedPowerTag.empty()) {
            g_claimedPowerTag.assign(powerTag);
            SKSE::log::info("CFT LOCAL POWER INTENT claimed tag={}", g_claimedPowerTag);
            return true;
        }

        if (g_claimedPowerTag == powerTag) {
            return true;
        }

        SKSE::log::info(
            "CFT REMOTE POWER suppressed tag={} claimed={} reason=tag-mismatch",
            powerTag,
            g_claimedPowerTag);
        return false;
    }

    bool AuthorizeNestedPower(std::string_view powerTag)
    {
        if (powerTag.empty()) {
            return false;
        }

        std::scoped_lock lock(g_mutex);

        // This is only invoked by the already-authorized local Resourcefulness
        // script after the player chooses a submenu entry. Re-arm here so taking
        // more than two seconds to choose does not suppress the legitimate nested
        // Harvest / Build / Create cast.
        g_claimedPowerTag.assign(powerTag);
        g_expiresAt = std::chrono::steady_clock::now() + kIntentLifetime;
        SKSE::log::info("CFT LOCAL POWER INTENT nested-authorized tag={} window_ms=2000", g_claimedPowerTag);
        return true;
    }

    bool Consume()
    {
        return ConsumePower("build", RE::PlayerCharacter::GetSingleton());
    }
}
