#include "PCH.h"
#include "STRPMClient.h"

#include "CampfireSync.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr char kChannel[] = "campfiretogether";
    }

    STRPMClient& STRPMClient::GetSingleton()
    {
        static STRPMClient instance;
        return instance;
    }

    bool STRPMClient::Initialize()
    {
        if (_api) {
            return true;
        }

        const auto* api = STRPM::LoadFromModule();
        if (!api || !api->registerChannel || !api->send) {
            SKSE::log::warn("CFT STRPM unavailable; synchronization disabled until next initialization attempt");
            return false;
        }

        STRPM::ListenerHandle listener{};
        const auto result = api->registerChannel(kChannel, &STRPMClient::OnMessage, this, &listener);
        if (result != STRPM::Result::kOk) {
            SKSE::log::warn("CFT STRPM register failed channel={} result={}", kChannel, STRPM::ResultToString(result));
            return false;
        }

        _api = api;
        _resolver = STRPM::LoadProxyResolverFromModule();
        _listener = listener;

        if (_api->setLocalDisplayName) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                const char* name = player->GetName();
                if (name && *name) {
                    _api->setLocalDisplayName(name);
                }
            }
        }

        SKSE::log::info("CFT STRPM READY channel={} apiVersion={} proxyResolver={}", kChannel, _api->version, _resolver ? 1 : 0);
        return true;
    }

    void STRPMClient::Shutdown()
    {
        if (_api && _listener.value != 0 && _api->unregisterChannel) {
            _api->unregisterChannel(_listener);
        }
        _listener = {};
        _resolver = nullptr;
        _api = nullptr;
    }

    bool STRPMClient::Send(const Protocol::Packet& packet) const
    {
        if (!_api || !_api->send || !Protocol::IsValid(packet)) {
            return false;
        }

        STRPM::Target target{};
        target.kind = STRPM::TargetKind::kAllPlayers;

        const auto result = _api->send(
            kChannel,
            target,
            &packet,
            sizeof(packet),
            STRPM::kMessageReliable | STRPM::kMessageOrdered);

        if (result != STRPM::Result::kOk) {
            SKSE::log::warn(
                "CFT STRPM TX failed result={} type={} event={} base={}:{:08X}",
                STRPM::ResultToString(result),
                static_cast<unsigned>(packet.type),
                packet.eventID,
                packet.basePluginName,
                packet.baseLocalFormID);
            return false;
        }

        SKSE::log::info(
            "CFT STRPM TX type={} event={} base={}:{:08X} tent={}",
            static_cast<unsigned>(packet.type),
            packet.eventID,
            packet.basePluginName,
            packet.baseLocalFormID,
            (packet.flags & Protocol::kTent) ? 1 : 0);
        return true;
    }

    std::optional<RE::FormID> STRPMClient::ResolveProxy(STRPM::ConnectionID connectionID) const
    {
        if (!_resolver || !_resolver->resolve || connectionID == 0) {
            return std::nullopt;
        }

        STRPM::ProxyFormID formID = STRPM::kInvalidProxyFormID;
        const auto result = _resolver->resolve(connectionID, &formID);
        if (result != STRPM::Result::kOk || formID == STRPM::kInvalidProxyFormID) {
            return std::nullopt;
        }
        return static_cast<RE::FormID>(formID);
    }

    void STRPM_CALL STRPMClient::OnMessage(const STRPM::Message* message, void* userData)
    {
        if (message && userData) {
            static_cast<STRPMClient*>(userData)->HandleMessage(*message);
        }
    }

    void STRPMClient::HandleMessage(const STRPM::Message& message)
    {
        if (!message.data || message.size != sizeof(Protocol::Packet) || message.sender.connectionID == 0) {
            return;
        }

        Protocol::Packet packet{};
        std::memcpy(&packet, message.data, sizeof(packet));
        if (!Protocol::IsValid(packet)) {
            SKSE::log::warn("CFT STRPM RX rejected malformed packet connection={} bytes={}", message.sender.connectionID, message.size);
            return;
        }

        const auto connectionID = message.sender.connectionID;
        SKSE::log::info(
            "CFT STRPM RX connection={} type={} event={} base={}:{:08X}",
            connectionID,
            static_cast<unsigned>(packet.type),
            packet.eventID,
            packet.basePluginName,
            packet.baseLocalFormID);

        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([connectionID, packet]() {
                CampfireSync::GetSingleton().HandleRemote(connectionID, packet);
            });
        }
    }
}
