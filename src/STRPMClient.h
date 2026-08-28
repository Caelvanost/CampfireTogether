#pragma once

#include "CampfireTogether/Protocol.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace CampfireTogether
{
    class STRPMClient
    {
    public:
        static STRPMClient& GetSingleton();

        bool Initialize();
        void Shutdown();

        [[nodiscard]] bool IsAvailable() const noexcept { return _api != nullptr; }
        [[nodiscard]] bool Send(const Protocol::Packet& packet) const;
        [[nodiscard]] std::optional<RE::FormID> ResolveProxy(STRPM::ConnectionID connectionID) const;

    private:
        static void STRPM_CALL OnMessage(const STRPM::Message* message, void* userData);
        void HandleMessage(const STRPM::Message& message);

        const STRPM::Interface* _api{ nullptr };
        const STRPM::ProxyResolverInterface* _resolver{ nullptr };
        STRPM::ListenerHandle _listener{};
    };
}
