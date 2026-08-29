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
        [[nodiscard]] bool SendTo(STRPM::ConnectionID connectionID, const Protocol::Packet& packet) const;
        [[nodiscard]] std::optional<RE::FormID> ResolveProxy(STRPM::ConnectionID connectionID) const;

        void RequestSnapshots();
        void RequestSnapshot(STRPM::ConnectionID connectionID);

    private:
        static void STRPM_CALL OnMessage(const STRPM::Message* message, void* userData);
        static void STRPM_CALL OnProxyMapping(const STRPM::ProxyMappingEvent* event, void* userData);

        void HandleMessage(const STRPM::Message& message);
        void HandleProxyMapping(const STRPM::ProxyMappingEvent& event);
        [[nodiscard]] bool SendImpl(STRPM::Target target, const Protocol::Packet& packet) const;
        [[nodiscard]] Protocol::Packet MakeSnapshotRequest();

        const STRPM::Interface* _api{ nullptr };
        const STRPM::ProxyResolverInterface* _resolver{ nullptr };
        STRPM::ListenerHandle _listener{};
        bool _proxyListenerRegistered{ false };
        std::atomic<std::uint64_t> _nextSnapshotRequestID{ 1 };
    };
}
