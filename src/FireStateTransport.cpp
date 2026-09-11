#include "PCH.h"
#include "FireStateTransport.h"

#include "FireStateSync.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr char kChannel[] = "campfiretogether.fire";
        constexpr auto kProbeCooldown = std::chrono::seconds(10);
    }

    FireStateTransport& FireStateTransport::GetSingleton()
    {
        static FireStateTransport instance;
        return instance;
    }

    bool FireStateTransport::Initialize()
    {
        if (_api) {
            return true;
        }

        const auto* api = STRPM::LoadFromModule();
        if (!api || !api->registerChannel || !api->send) {
            SKSE::log::warn("CFT FIRE STRPM unavailable");
            return false;
        }

        STRPM::ListenerHandle listener{};
        const auto result = api->registerChannel(kChannel, &FireStateTransport::OnMessage, this, &listener);
        if (result != STRPM::Result::kOk) {
            SKSE::log::warn(
                "CFT FIRE STRPM register failed channel={} result={}",
                kChannel,
                STRPM::ResultToString(result));
            return false;
        }

        _api = api;
        _listener = listener;
        SKSE::log::info(
            "CFT FIRE STRPM READY channel={} protocol={}",
            kChannel,
            FireStateProtocol::kVersion);
        return true;
    }

    void FireStateTransport::Shutdown()
    {
        if (_api && _listener.value != 0 && _api->unregisterChannel) {
            _api->unregisterChannel(_listener);
        }
        _listener = {};
        _api = nullptr;
        {
            std::scoped_lock lock(_probeMutex);
            _lastProbeAttempt = {};
        }
    }

    bool FireStateTransport::Send(const FireStateProtocol::Packet& packet) const
    {
        STRPM::Target target{};
        target.kind = STRPM::TargetKind::kAllPlayers;
        return SendImpl(target, packet);
    }

    bool FireStateTransport::SendTo(
        STRPM::ConnectionID connectionID,
        const FireStateProtocol::Packet& packet) const
    {
        if (connectionID == 0) {
            return false;
        }

        STRPM::Target target{};
        target.kind = STRPM::TargetKind::kPlayer;
        target.connectionID = connectionID;
        return SendImpl(target, packet);
    }

    bool FireStateTransport::SendImpl(
        STRPM::Target target,
        const FireStateProtocol::Packet& packet) const
    {
        if (!_api || !_api->send || !FireStateProtocol::IsValid(packet)) {
            return false;
        }

        const auto result = _api->send(
            kChannel,
            target,
            &packet,
            sizeof(packet),
            STRPM::kMessageReliable | STRPM::kMessageOrdered);

        if (result != STRPM::Result::kOk) {
            if (result != STRPM::Result::kNotConnected &&
                result != STRPM::Result::kTargetNotFound) {
                SKSE::log::warn(
                    "CFT FIRE TX failed result={} target={} type={} object={:016X}:{} rev={}",
                    STRPM::ResultToString(result),
                    target.connectionID,
                    static_cast<unsigned>(packet.type),
                    packet.originNodeID,
                    packet.objectID,
                    packet.revision);
            }
            return false;
        }

        if (FireStateProtocol::IsStatePacket(packet)) {
            SKSE::log::info(
                "CFT FIRE TX target={} object={:016X}:{} rev={} writer={:016X} stage={} size={} remaining={:.2f} snapshot={}",
                target.connectionID,
                packet.originNodeID,
                packet.objectID,
                packet.revision,
                packet.writerNodeID,
                packet.stage,
                packet.size,
                packet.remainingHours,
                packet.snapshotID);
        }
        return true;
    }

    FireStateProtocol::Packet FireStateTransport::MakeSnapshotRequest()
    {
        FireStateProtocol::Packet packet{};
        packet.type = FireStateProtocol::PacketType::kSnapshotRequest;
        packet.snapshotID = _nextSnapshotRequestID.fetch_add(1);
        return packet;
    }

    void FireStateTransport::RequestSnapshots()
    {
        if (!_api) {
            return;
        }
        auto packet = MakeSnapshotRequest();
        if (Send(packet)) {
            SKSE::log::info("CFT FIRE SNAPSHOT REQUEST broadcast request={}", packet.snapshotID);
        }
    }

    void FireStateTransport::RequestSnapshot(STRPM::ConnectionID connectionID)
    {
        if (!_api || connectionID == 0) {
            return;
        }
        auto packet = MakeSnapshotRequest();
        if (SendTo(connectionID, packet)) {
            SKSE::log::info(
                "CFT FIRE SNAPSHOT REQUEST targeted connection={} request={}",
                connectionID,
                packet.snapshotID);
        }
    }

    void FireStateTransport::ProbeStateExchange()
    {
        if (!_api || !_api->getLocalConnectionID) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        {
            std::scoped_lock lock(_probeMutex);
            if (_lastProbeAttempt.time_since_epoch().count() != 0 &&
                now - _lastProbeAttempt < kProbeCooldown) {
                return;
            }
            _lastProbeAttempt = now;
        }

        STRPM::ConnectionID localConnectionID = 0;
        if (_api->getLocalConnectionID(&localConnectionID) != STRPM::Result::kOk ||
            localConnectionID == 0) {
            return;
        }

        auto packet = MakeSnapshotRequest();
        if (Send(packet)) {
            SKSE::log::info(
                "CFT FIRE BOOTSTRAP connected localConnection={} request={} trigger=cell-load",
                localConnectionID,
                packet.snapshotID);
        }
    }

    void STRPM_CALL FireStateTransport::OnMessage(const STRPM::Message* message, void* userData)
    {
        if (message && userData) {
            static_cast<FireStateTransport*>(userData)->HandleMessage(*message);
        }
    }

    void FireStateTransport::HandleMessage(const STRPM::Message& message)
    {
        if (!message.data || message.sender.connectionID == 0) {
            return;
        }

        if (message.size != sizeof(FireStateProtocol::Packet)) {
            SKSE::log::warn(
                "CFT FIRE RX incompatible connection={} bytes={} expected={}",
                message.sender.connectionID,
                message.size,
                sizeof(FireStateProtocol::Packet));
            return;
        }

        FireStateProtocol::Packet packet{};
        std::memcpy(&packet, message.data, sizeof(packet));
        if (!FireStateProtocol::IsValid(packet)) {
            SKSE::log::warn(
                "CFT FIRE RX malformed connection={} bytes={}",
                message.sender.connectionID,
                message.size);
            return;
        }

        if (FireStateProtocol::IsStatePacket(packet)) {
            SKSE::log::info(
                "CFT FIRE RX connection={} object={:016X}:{} rev={} writer={:016X} stage={} size={} remaining={:.2f} snapshot={}",
                message.sender.connectionID,
                packet.originNodeID,
                packet.objectID,
                packet.revision,
                packet.writerNodeID,
                packet.stage,
                packet.size,
                packet.remainingHours,
                packet.snapshotID);
        }

        const auto sender = message.sender.connectionID;
        auto dispatch = [sender, packet]() {
            FireStateSync::GetSingleton().HandleRemote(sender, packet);
        };

        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask(std::move(dispatch));
        } else {
            dispatch();
        }
    }
}
