#pragma once

#include "CampfireTogether/FireStateProtocol.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace CampfireTogether
{
    class FireStateTransport
    {
    public:
        static FireStateTransport& GetSingleton();

        bool Initialize();
        void Shutdown();

        [[nodiscard]] bool Send(const FireStateProtocol::Packet& packet) const;
        [[nodiscard]] bool SendTo(STRPM::ConnectionID connectionID, const FireStateProtocol::Packet& packet) const;

        void RequestSnapshots();
        void RequestSnapshot(STRPM::ConnectionID connectionID);
        void ProbeStateExchange();

    private:
        static void STRPM_CALL OnMessage(const STRPM::Message* message, void* userData);
        void HandleMessage(const STRPM::Message& message);
        [[nodiscard]] bool SendImpl(STRPM::Target target, const FireStateProtocol::Packet& packet) const;
        [[nodiscard]] FireStateProtocol::Packet MakeSnapshotRequest();

        const STRPM::Interface* _api{ nullptr };
        STRPM::ListenerHandle _listener{};
        std::atomic<std::uint64_t> _nextSnapshotRequestID{ 1 };
        std::mutex _probeMutex;
        std::chrono::steady_clock::time_point _lastProbeAttempt{};
    };
}
