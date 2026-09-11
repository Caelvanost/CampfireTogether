#include "PCH.h"
#include "FireStateSync.h"

#include "FireStateTransport.h"
#include "SharedCampSync.h"

namespace CampfireTogether
{
    namespace
    {
        [[nodiscard]] FireStateProtocol::Packet MakeControlPacket(
            FireStateProtocol::PacketType type,
            std::uint64_t snapshotID)
        {
            FireStateProtocol::Packet packet{};
            packet.type = type;
            packet.snapshotID = snapshotID;
            return packet;
        }

        void WriteIdentity(
            FireStateProtocol::FormIdentity& target,
            const std::string& pluginName,
            RE::FormID localFormID)
        {
            target = {};
            if (pluginName.empty() ||
                localFormID == 0 ||
                pluginName.size() >= FireStateProtocol::kPluginNameCapacity) {
                return;
            }
            target.localFormID = localFormID;
            std::memcpy(target.pluginName, pluginName.c_str(), pluginName.size() + 1);
        }
    }

    FireStateSync& FireStateSync::GetSingleton()
    {
        static FireStateSync instance;
        return instance;
    }

    std::optional<FireStateSync::Key> FireStateSync::GetKey(RE::TESObjectREFR* reference) const
    {
        std::uint64_t originNodeID = 0;
        std::uint64_t objectID = 0;
        bool isTent = false;
        if (!SharedCampSync::GetSingleton().TryGetCampKey(
                reference,
                originNodeID,
                objectID,
                isTent) ||
            isTent) {
            return std::nullopt;
        }
        return Key{ originNodeID, objectID };
    }

    std::optional<FireStateSync::FormIdentity> FireStateSync::DescribeForm(RE::TESForm* form) const
    {
        if (!form || form->GetFormID() == 0) {
            return std::nullopt;
        }
        auto* owner = form->GetFile(0);
        if (!owner || !owner->fileName[0]) {
            return std::nullopt;
        }
        const auto localFormID = form->GetLocalFormID();
        if (localFormID == 0) {
            return std::nullopt;
        }
        return FormIdentity{ owner->fileName, localFormID };
    }

    RE::TESForm* FireStateSync::ResolveForm(const FormIdentity& identity) const
    {
        if (identity.pluginName.empty() || identity.localFormID == 0) {
            return nullptr;
        }
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        return dataHandler ? dataHandler->LookupForm(identity.localFormID, identity.pluginName) : nullptr;
    }

    bool FireStateSync::MeaningfullyDifferent(const State& current, const State& observed) const
    {
        return current.stage != observed.stage ||
               current.size != observed.size ||
               current.fuelLit != observed.fuelLit ||
               current.fuelUnlit != observed.fuelUnlit ||
               current.light != observed.light;
    }

    bool FireStateSync::Merge(const State& incoming, bool fromNetwork)
    {
        bool changed = false;
        {
            std::scoped_lock lock(_mutex);
            const auto it = _states.find(incoming.key);
            if (it == _states.end()) {
                _states.emplace(incoming.key, incoming);
                changed = true;
            } else {
                const auto& current = it->second;
                const bool newer =
                    incoming.revision > current.revision ||
                    (incoming.revision == current.revision &&
                     incoming.writerNodeID > current.writerNodeID);
                if (newer) {
                    it->second = incoming;
                    changed = true;
                }
            }
        }

        if (changed) {
            SKSE::log::info(
                "CFT FIRE MERGE object={:016X}:{} rev={} writer={:016X} stage={} size={} remaining={:.2f} source={}",
                incoming.key.originNodeID,
                incoming.key.objectID,
                incoming.revision,
                incoming.writerNodeID,
                incoming.stage,
                incoming.size,
                incoming.remainingHours,
                fromNetwork ? "network" : "local");
        }
        return changed;
    }

    void FireStateSync::TrackReference(const Key& key, RE::TESObjectREFR* reference)
    {
        if (!reference || reference->IsMarkedForDeletion()) {
            return;
        }
        std::scoped_lock lock(_mutex);
        _tracked.insert_or_assign(key, reference->CreateRefHandle());
    }

    void FireStateSync::PruneInvalidTracked()
    {
        std::scoped_lock lock(_mutex);
        for (auto it = _tracked.begin(); it != _tracked.end();) {
            auto reference = it->second.get();
            if (!reference || reference->IsMarkedForDeletion() ||
                !SharedCampSync::GetSingleton().IsCampActive(
                    it->first.originNodeID,
                    it->first.objectID)) {
                _appliedRevision.erase(it->first);
                it = _tracked.erase(it);
            } else {
                ++it;
            }
        }
    }

    void FireStateSync::OnCellFullyLoaded(RE::TESObjectCELL* cell)
    {
        if (!cell || !cell->IsAttached()) {
            return;
        }

        std::size_t tracked = 0;
        cell->ForEachReference([&](RE::TESObjectREFR& reference) {
            const auto key = GetKey(std::addressof(reference));
            if (key) {
                TrackReference(*key, std::addressof(reference));
                ++tracked;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });

        PruneInvalidTracked();
        if (tracked > 0) {
            SKSE::log::info(
                "CFT FIRE CELL TRACK cell={:08X} campfires={}",
                cell->GetFormID(),
                tracked);
        }
    }

    void FireStateSync::ReportObserved(
        RE::TESObjectREFR* reference,
        std::int32_t stage,
        std::int32_t size,
        float remainingHours,
        RE::TESForm* fuelLit,
        RE::TESForm* fuelUnlit,
        RE::TESForm* light)
    {
        const auto key = GetKey(reference);
        if (!key) {
            return;
        }
        TrackReference(*key, reference);

        stage = std::clamp(stage, 0, 5);
        size = std::clamp(size, 0, 4);
        if (stage == 5) {
            // Lighting is a short-lived local animation. Synchronize the stable
            // "fuel + tinder" state until Campfire commits stage 2 or 3.
            stage = 4;
        }
        remainingHours = std::max(0.0f, remainingHours);

        State observed{};
        observed.key = *key;
        observed.stage = stage;
        observed.size = size;
        observed.remainingHours = remainingHours;
        if (const auto identity = DescribeForm(fuelLit)) {
            observed.fuelLit = *identity;
        }
        if (const auto identity = DescribeForm(fuelUnlit)) {
            observed.fuelUnlit = *identity;
        }
        if (const auto identity = DescribeForm(light)) {
            observed.light = *identity;
        }

        const auto localNodeID = SharedCampSync::GetSingleton().GetLocalNodeID();
        bool broadcast = false;
        State outgoing{};
        {
            std::scoped_lock lock(_mutex);
            const auto currentIt = _states.find(*key);
            if (currentIt == _states.end()) {
                observed.revision = 1;
                observed.writerNodeID = localNodeID;
                _states.emplace(*key, observed);
                _appliedRevision.insert_or_assign(*key, observed.revision);
                outgoing = observed;
                broadcast = true;
            } else {
                const auto appliedIt = _appliedRevision.find(*key);
                const auto appliedRevision = appliedIt != _appliedRevision.end() ? appliedIt->second : 0;
                if (appliedRevision < currentIt->second.revision) {
                    return;
                }

                if (MeaningfullyDifferent(currentIt->second, observed)) {
                    observed.revision = currentIt->second.revision + 1;
                    observed.writerNodeID = localNodeID;
                    currentIt->second = observed;
                    _appliedRevision.insert_or_assign(*key, observed.revision);
                    outgoing = observed;
                    broadcast = true;
                } else {
                    // Timer drift alone is not a network revision. Keep the latest
                    // observed value locally so a future state transition carries a
                    // useful resume point without creating ping-pong between clients.
                    currentIt->second.remainingHours = remainingHours;
                }
            }
        }

        if (broadcast) {
            SKSE::log::info(
                "CFT FIRE LOCAL CHANGE object={:016X}:{} rev={} stage={} size={} remaining={:.2f}",
                outgoing.key.originNodeID,
                outgoing.key.objectID,
                outgoing.revision,
                outgoing.stage,
                outgoing.size,
                outgoing.remainingHours);
            BroadcastState(outgoing);
        }
    }

    std::optional<FireStateSync::State> FireStateSync::GetStateForReference(
        RE::TESObjectREFR* reference) const
    {
        const auto key = GetKey(reference);
        if (!key) {
            return std::nullopt;
        }
        std::scoped_lock lock(_mutex);
        const auto it = _states.find(*key);
        return it != _states.end() ? std::optional<State>{ it->second } : std::nullopt;
    }

    std::int32_t FireStateSync::GetTrackedCount() const
    {
        std::scoped_lock lock(_mutex);
        std::int32_t count = 0;
        for (const auto& [key, handle] : _tracked) {
            (void)key;
            if (auto reference = handle.get(); reference && !reference->IsMarkedForDeletion()) {
                ++count;
            }
        }
        return count;
    }

    RE::TESObjectREFR* FireStateSync::GetTracked(std::int32_t index) const
    {
        if (index < 0) {
            return nullptr;
        }

        std::vector<RE::NiPointer<RE::TESObjectREFR>> references;
        {
            std::scoped_lock lock(_mutex);
            references.reserve(_tracked.size());
            for (const auto& [key, handle] : _tracked) {
                (void)key;
                auto reference = handle.get();
                if (reference && !reference->IsMarkedForDeletion()) {
                    references.push_back(reference);
                }
            }
        }

        std::sort(
            references.begin(),
            references.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs->GetFormID() < rhs->GetFormID();
            });

        const auto requested = static_cast<std::size_t>(index);
        return requested < references.size() ? references[requested].get() : nullptr;
    }

    bool FireStateSync::NeedsApply(RE::TESObjectREFR* reference) const
    {
        const auto key = GetKey(reference);
        if (!key) {
            return false;
        }
        std::scoped_lock lock(_mutex);
        const auto stateIt = _states.find(*key);
        if (stateIt == _states.end()) {
            return false;
        }
        const auto appliedIt = _appliedRevision.find(*key);
        const auto applied = appliedIt != _appliedRevision.end() ? appliedIt->second : 0;
        return applied < stateIt->second.revision;
    }

    std::int32_t FireStateSync::GetDesiredStage(RE::TESObjectREFR* reference) const
    {
        const auto state = GetStateForReference(reference);
        return state ? state->stage : 0;
    }

    std::int32_t FireStateSync::GetDesiredSize(RE::TESObjectREFR* reference) const
    {
        const auto state = GetStateForReference(reference);
        return state ? state->size : 0;
    }

    float FireStateSync::GetDesiredRemainingHours(RE::TESObjectREFR* reference) const
    {
        const auto state = GetStateForReference(reference);
        return state ? state->remainingHours : 0.0f;
    }

    RE::TESForm* FireStateSync::GetDesiredFuelLit(RE::TESObjectREFR* reference) const
    {
        const auto state = GetStateForReference(reference);
        return state ? ResolveForm(state->fuelLit) : nullptr;
    }

    RE::TESForm* FireStateSync::GetDesiredFuelUnlit(RE::TESObjectREFR* reference) const
    {
        const auto state = GetStateForReference(reference);
        return state ? ResolveForm(state->fuelUnlit) : nullptr;
    }

    RE::TESForm* FireStateSync::GetDesiredLight(RE::TESObjectREFR* reference) const
    {
        const auto state = GetStateForReference(reference);
        return state ? ResolveForm(state->light) : nullptr;
    }

    void FireStateSync::AcknowledgeApplied(RE::TESObjectREFR* reference)
    {
        const auto key = GetKey(reference);
        if (!key) {
            return;
        }
        std::scoped_lock lock(_mutex);
        const auto stateIt = _states.find(*key);
        if (stateIt != _states.end()) {
            _appliedRevision.insert_or_assign(*key, stateIt->second.revision);
            SKSE::log::info(
                "CFT FIRE APPLY ACK object={:016X}:{} rev={}",
                key->originNodeID,
                key->objectID,
                stateIt->second.revision);
        }
    }

    void FireStateSync::BroadcastState(const State& state)
    {
        FireStateProtocol::Packet packet{};
        packet.type = FireStateProtocol::PacketType::kState;
        packet.originNodeID = state.key.originNodeID;
        packet.objectID = state.key.objectID;
        packet.revision = state.revision;
        packet.writerNodeID = state.writerNodeID;
        packet.stage = static_cast<std::uint8_t>(state.stage);
        packet.size = static_cast<std::uint8_t>(state.size);
        packet.remainingHours = state.remainingHours;
        WriteIdentity(packet.fuelLit, state.fuelLit.pluginName, state.fuelLit.localFormID);
        WriteIdentity(packet.fuelUnlit, state.fuelUnlit.pluginName, state.fuelUnlit.localFormID);
        WriteIdentity(packet.light, state.light.pluginName, state.light.localFormID);
        (void)FireStateTransport::GetSingleton().Send(packet);
    }

    void FireStateSync::HandleRemote(
        STRPM::ConnectionID sender,
        const FireStateProtocol::Packet& packet)
    {
        switch (packet.type) {
        case FireStateProtocol::PacketType::kState: {
            State incoming{};
            incoming.key = Key{ packet.originNodeID, packet.objectID };
            incoming.revision = packet.revision;
            incoming.writerNodeID = packet.writerNodeID;
            incoming.stage = packet.stage;
            incoming.size = packet.size;
            incoming.remainingHours = packet.remainingHours;
            if (packet.fuelLit.localFormID != 0 && packet.fuelLit.pluginName[0] != '\0') {
                incoming.fuelLit = { packet.fuelLit.pluginName, packet.fuelLit.localFormID };
            }
            if (packet.fuelUnlit.localFormID != 0 && packet.fuelUnlit.pluginName[0] != '\0') {
                incoming.fuelUnlit = { packet.fuelUnlit.pluginName, packet.fuelUnlit.localFormID };
            }
            if (packet.light.localFormID != 0 && packet.light.pluginName[0] != '\0') {
                incoming.light = { packet.light.pluginName, packet.light.localFormID };
            }

            if (packet.snapshotID != 0) {
                std::scoped_lock lock(_mutex);
                if (const auto it = _remoteSnapshots.find(sender);
                    it != _remoteSnapshots.end() && it->second.snapshotID == packet.snapshotID) {
                    ++it->second.seen;
                }
            }
            (void)Merge(incoming, true);
            break;
        }
        case FireStateProtocol::PacketType::kSnapshotRequest:
            SendSnapshot(sender);
            break;
        case FireStateProtocol::PacketType::kSnapshotBegin: {
            std::scoped_lock lock(_mutex);
            _remoteSnapshots.insert_or_assign(sender, SnapshotReceiveState{ packet.snapshotID, 0 });
            SKSE::log::info(
                "CFT FIRE SNAPSHOT RX begin connection={} id={}",
                sender,
                packet.snapshotID);
            break;
        }
        case FireStateProtocol::PacketType::kSnapshotEnd: {
            std::size_t seen = 0;
            {
                std::scoped_lock lock(_mutex);
                if (const auto it = _remoteSnapshots.find(sender);
                    it != _remoteSnapshots.end() && it->second.snapshotID == packet.snapshotID) {
                    seen = it->second.seen;
                    _remoteSnapshots.erase(it);
                }
            }
            SKSE::log::info(
                "CFT FIRE SNAPSHOT RX complete connection={} id={} seen={}",
                sender,
                packet.snapshotID,
                seen);
            break;
        }
        default:
            break;
        }
    }

    void FireStateSync::SendSnapshot(std::optional<STRPM::ConnectionID> target)
    {
        std::vector<State> states;
        {
            std::scoped_lock lock(_mutex);
            states.reserve(_states.size());
            for (const auto& [key, state] : _states) {
                if (SharedCampSync::GetSingleton().IsCampActive(key.originNodeID, key.objectID)) {
                    states.push_back(state);
                }
            }
        }

        const auto snapshotID = _nextSnapshotID.fetch_add(1);
        const auto sendPacket = [&](const FireStateProtocol::Packet& packet) {
            return target ?
                FireStateTransport::GetSingleton().SendTo(*target, packet) :
                FireStateTransport::GetSingleton().Send(packet);
        };

        if (!sendPacket(MakeControlPacket(FireStateProtocol::PacketType::kSnapshotBegin, snapshotID))) {
            return;
        }

        for (const auto& state : states) {
            FireStateProtocol::Packet packet{};
            packet.type = FireStateProtocol::PacketType::kState;
            packet.flags = FireStateProtocol::kSnapshot;
            packet.snapshotID = snapshotID;
            packet.originNodeID = state.key.originNodeID;
            packet.objectID = state.key.objectID;
            packet.revision = state.revision;
            packet.writerNodeID = state.writerNodeID;
            packet.stage = static_cast<std::uint8_t>(state.stage);
            packet.size = static_cast<std::uint8_t>(state.size);
            packet.remainingHours = state.remainingHours;
            WriteIdentity(packet.fuelLit, state.fuelLit.pluginName, state.fuelLit.localFormID);
            WriteIdentity(packet.fuelUnlit, state.fuelUnlit.pluginName, state.fuelUnlit.localFormID);
            WriteIdentity(packet.light, state.light.pluginName, state.light.localFormID);
            if (!sendPacket(packet)) {
                return;
            }
        }

        if (sendPacket(MakeControlPacket(FireStateProtocol::PacketType::kSnapshotEnd, snapshotID))) {
            SKSE::log::info(
                "CFT FIRE SNAPSHOT TX complete target={} id={} states={}",
                target.value_or(0),
                snapshotID,
                states.size());
        }
    }

    void FireStateSync::ClearRuntime()
    {
        std::scoped_lock lock(_mutex);
        _tracked.clear();
        _appliedRevision.clear();
        _remoteSnapshots.clear();
    }

    void FireStateSync::ClearAll()
    {
        std::scoped_lock lock(_mutex);
        _states.clear();
        _tracked.clear();
        _appliedRevision.clear();
        _remoteSnapshots.clear();
        _nextSnapshotID.store(1);
        SKSE::log::info("CFT FIRE STATE cleared");
    }
}
