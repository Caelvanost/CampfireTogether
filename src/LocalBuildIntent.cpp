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
            }

            SKSE::log::info(
                "CFT LOCAL BUILD INTENT armed action=Shout device={} id={}",
                static_cast<unsigned>(button.GetDevice()),
                button.GetIDCode());
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
            SKSE::log::warn("CFT LOCAL BUILD INTENT input sink unavailable: BSInputDeviceManager is null");
            return;
        }

        if (g_registered.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        inputManager->AddEventSink(std::addressof(InputSink::GetSingleton()));
        SKSE::log::info("CFT LOCAL BUILD INTENT input sink READY action=Shout window_ms=2000");
    }

    void Reset()
    {
        std::scoped_lock lock(g_mutex);
        g_expiresAt = {};
    }

    bool Consume()
    {
        std::scoped_lock lock(g_mutex);

        const auto now = std::chrono::steady_clock::now();
        if (g_expiresAt == std::chrono::steady_clock::time_point{} || g_expiresAt < now) {
            g_expiresAt = {};
            return false;
        }

        g_expiresAt = {};
        SKSE::log::info("CFT LOCAL BUILD INTENT consumed");
        return true;
    }
}
