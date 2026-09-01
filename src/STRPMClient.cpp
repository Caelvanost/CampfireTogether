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

        if (_resolver && _resolver->registerListener) {
            const auto listenerResult = _resolver->registerListener(&STRPMClient::OnProxyMapping, this);
            _proxyListenerRegistered = listenerResult == STRPM::Result::kOk;
            if (!_proxyListenerRegistered) {
                SKSE::log::warn(
                    "CFT STRPM proxy listener registration failed result={}",
                    STRPM::ResultToString(listenerResult));
            }
        }

        if (_api->setLocalDisplayName) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                const char* name = player->GetName();
                if (name && *name) {
                    _api->setLocalDisplayName(name);
                }
            }
        }

        SKSE::log::info(
            "CFT STRPM READY channel={} apiVersion={} proxyResolver={} proxyListener={}",
            kChannel,
            _api->version,
            _resolver ? 1 : 0,
            _proxyListenerRegistered ? 1 : 0);
        return true;
    }

    void STRPMClient::Shutdown()
    {
        if (_resolver && _proxyListenerRegistered && _resolver->unregisterListener) {
            _resolver->unregisterListener(&STRPMClient::OnProxyMapping, this);
        }
        _proxyListenerRegistered = false;

        if (_api && _listener.value != 0 && _api->unregisterChannel) {
            _api->unregisterChannel(_listener);
        }
        _listener = {};
        _resolver = nullptr;
        _api = nullptr;
        ForgetAllPeers();
    }

    bool STRPMClient::Send(const Protocol::Packet& packet) const
    {
        STRPM::Target target{};
        target.kind = STRPM::TargetKind::kAllPlayers;
        return SendImpl(target, packet);
    }

    bool STRPMClient::SendTo(STRPM::ConnectionID connectionID, const Protocol::Packet& packet) const
    {
        if (connectionID == 0) {
            return false;
        }

        STRPM::Target target{};
        target.kind = STRPM::TargetKind::kPlayer;
        target.connectionID = connectionID;
        return SendImpl(target, packet);
    }

    bool STRPMClient::SendImpl(STRPM::Target target, const Protocol::Packet& packet) const
    {
        if (!_api || !_api->send || !Protocol::IsValid(packet)) {
            return false;
        }

        const auto result = _api->send(
            kChannel,
            target,
            &packet,
            sizeof(packet),
            STRPM::kMessageReliable | STRPM::kMessageOrdered);

        if (result != STRPM::Result::kOk) {
            if (result == STRPM::Result::kNotConnected || result == STRPM::Result::kTargetNotFound) {
                SKSE::log::debug(
                    "CFT STRPM TX unavailable result={} targetKind={} target={} type={} snapshot={}",
                    STRPM::ResultToString(result),
                    static_cast<unsigned>(target.kind),
                    target.connectionID,
                    static_cast<unsigned>(packet.type),
                    packet.snapshotID);
            } else if (Protocol::IsObjectPacket(packet)) {
                SKSE::log::warn(
                    "CFT STRPM TX failed result={} target={} type={} event={} snapshot={} base={}:{:08X} cell={}:{:08X}",
                    STRPM::ResultToString(result),
                    target.connectionID,
                    static_cast<unsigned>(packet.type),
                    packet.eventID,
                    packet.snapshotID,
                    packet.basePluginName,
                    packet.baseLocalFormID,
                    packet.cellPluginName,
                    packet.cellLocalFormID);
            } else {
                SKSE::log::warn(
                    "CFT STRPM TX failed result={} target={} type={} snapshot={}",
                    STRPM::ResultToString(result),
                    target.connectionID,
                    static_cast<unsigned>(packet.type),
                    packet.snapshotID);
            }
            return false;
        }

        if (Protocol::IsObjectPacket(packet)) {
            SKSE::log::info(
                "CFT STRPM TX target={} type={} event={} snapshot={} base={}:{:08X} cell={}:{:08X} tent={}",
                target.connectionID,
                static_cast<unsigned>(packet.type),
                packet.eventID,
                packet.snapshotID,
                packet.basePluginName,
                packet.baseLocalFormID,
                packet.cellPluginName,
                packet.cellLocalFormID,
                (packet.flags & Protocol::kTent) ? 1 : 0);
        } else {
            SKSE::log::info(
                "CFT STRPM TX target={} type={} snapshot={}",
                target.connectionID,
                static_cast<unsigned>(packet.type),
                packet.snapshotID);
        }
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

    Protocol::Packet STRPMClient::MakeSnapshotRequest()
    {
        Protocol::Packet packet{};
        packet.type = Protocol::PacketType::kSnapshotRequest;
        packet.snapshotID = _nextSnapshotRequestID.fetch_add(1);
        return packet;
    }

    void STRPMClient::RequestSnapshots()
    {
        if (!_api) {
            return;
        }

        auto packet = MakeSnapshotRequest();
        if (Send(packet)) {
            SKSE::log::info("CFT SNAPSHOT REQUEST broadcast request={}", packet.snapshotID);
        }
    }

    void STRPMClient::RequestSnapshot(STRPM::ConnectionID connectionID)
    {
        if (!_api || connectionID == 0) {
            return;
        }

        auto packet = MakeSnapshotRequest();
        if (SendTo(connectionID, packet)) {
            SKSE::log::info(
                "CFT SNAPSHOT REQUEST targeted connection={} request={}",
                connectionID,
                packet.snapshotID);
        }
    }

    bool STRPMClient::MarkPeerObserved(STRPM::ConnectionID connectionID)
    {
        if (connectionID == 0) {
            return false;
        }
        std::scoped_lock lock(_peerMutex);
        return _observedPeers.insert(connectionID).second;
    }

    void STRPMClient::ForgetPeer(STRPM::ConnectionID connectionID)
    {
        if (connectionID == 0) {
            return;
        }
        std::scoped_lock lock(_peerMutex);
        _observedPeers.erase(connectionID);
    }

    void STRPMClient::ForgetAllPeers()
    {
        std::scoped_lock lock(_peerMutex);
        _observedPeers.clear();
    }

    void STRPM_CALL STRPMClient::OnMessage(const STRPM::Message* message, void* userData)
    {
        if (message && userData) {
            static_cast<STRPMClient*>(userData)->HandleMessage(*message);
        }
    }

    void STRPM_CALL STRPMClient::OnProxyMapping(const STRPM::ProxyMappingEvent* event, void* userData)
    {
        if (!event || !userData) {
            return;
        }

        const auto eventCopy = *event;
        auto dispatch = [client = static_cast<STRPMClient*>(userData), eventCopy]() {
            client->HandleProxyMapping(eventCopy);
        };

        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask(std::move(dispatch));
        } else {
            dispatch();
        }
    }

    void STRPMClient::HandleProxyMapping(const STRPM::ProxyMappingEvent& event)
    {
        switch (event.type) {
        case STRPM::ProxyMappingEventType::kAdded:
            if (event.connectionID != 0 && event.newFormID != STRPM::kInvalidProxyFormID) {
                SKSE::log::info(
                    "CFT STRPM PROXY added connection={} proxy={:08X}",
                    event.connectionID,
                    event.newFormID);
                (void)MarkPeerObserved(event.connectionID);
                CampfireSync::GetSingleton().OnPeerAvailable(event.connectionID);
                CampfireSync::GetSingleton().RefreshRemoteExteriorAtPlayer();
            }
            break;
        case STRPM::ProxyMappingEventType::kUpdated:
            if (event.connectionID != 0 && event.newFormID != STRPM::kInvalidProxyFormID) {
                SKSE::log::info(
                    "CFT STRPM PROXY updated connection={} old={:08X} new={:08X}",
                    event.connectionID,
                    event.oldFormID,
                    event.newFormID);
                (void)MarkPeerObserved(event.connectionID);
                CampfireSync::GetSingleton().OnPeerAvailable(event.connectionID);
                CampfireSync::GetSingleton().RefreshRemoteExteriorAtPlayer();
            } else if (event.connectionID != 0) {
                ForgetPeer(event.connectionID);
                SKSE::log::info(
                    "CFT STRPM PROXY unavailable connection={} preserving remote camp state",
                    event.connectionID);
            }
            break;
        case STRPM::ProxyMappingEventType::kRemoved:
            SKSE::log::info(
                "CFT STRPM PROXY removed connection={} old={:08X} preserving remote camp state",
                event.connectionID,
                event.oldFormID);
            ForgetPeer(event.connectionID);
            break;
        case STRPM::ProxyMappingEventType::kCleared:
            SKSE::log::info("CFT STRPM PROXY mappings cleared; preserving remote camp state");
            ForgetAllPeers();
            break;
        default:
            break;
        }
    }

    void STRPMClient::HandleMessage(const STRPM::Message& message)
    {
        if (!message.data || message.sender.connectionID == 0) {
            return;
        }

        if (message.size != sizeof(Protocol::Packet)) {
            SKSE::log::warn(
                "CFT STRPM RX rejected incompatible packet connection={} bytes={} expected={}",
                message.sender.connectionID,
                message.size,
                sizeof(Protocol::Packet));
            return;
        }

        Protocol::Packet packet{};
        std::memcpy(&packet, message.data, sizeof(packet));
        if (!Protocol::IsValid(packet)) {
            SKSE::log::warn(
                "CFT STRPM RX rejected malformed packet connection={} bytes={}",
                message.sender.connectionID,
                message.size);
            return;
        }

        const auto connectionID = message.sender.connectionID;
        const bool firstObservedPacket = MarkPeerObserved(connectionID);

        if (Protocol::IsObjectPacket(packet)) {
            SKSE::log::info(
                "CFT STRPM RX connection={} type={} event={} snapshot={} base={}:{:08X} cell={}:{:08X}",
                connectionID,
                static_cast<unsigned>(packet.type),
                packet.eventID,
                packet.snapshotID,
                packet.basePluginName,
                packet.baseLocalFormID,
                packet.cellPluginName,
                packet.cellLocalFormID);
        } else {
            SKSE::log::info(
                "CFT STRPM RX connection={} type={} snapshot={}",
                connectionID,
                static_cast<unsigned>(packet.type),
                packet.snapshotID);
        }

        auto dispatch = [connectionID, packet, firstObservedPacket]() {
            auto& sync = CampfireSync::GetSingleton();
            if (firstObservedPacket) {
                SKSE::log::info(
                    "CFT STRPM PEER discovered from RX connection={} fallback=1",
                    connectionID);
                sync.OnPeerAvailable(connectionID);
            }
            sync.HandleRemote(connectionID, packet);
            if (packet.type == Protocol::PacketType::kPlace) {
                sync.RefreshRemoteExteriorAtPlayer();
            }
        };

        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask(std::move(dispatch));
        } else {
            dispatch();
        }
    }
}
