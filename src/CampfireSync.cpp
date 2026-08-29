#include "PCH.h"
#include "CampfireSync.h"

#include "STRPMClient.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr std::size_t kMaxSuppressedRemovals = 64;
        constexpr std::size_t kMaxPersistedPlacements = 100000;
        constexpr float kRemovalMatchRadius = 64.0f;
        constexpr auto kRemovalSuppressionLifetime = std::chrono::seconds(5);

        constexpr std::uint32_t kStateRecordType = 0x54415453;  // "STAT"
        constexpr std::uint32_t kStateRecordVersion = 2;

        struct FormIdentity
        {
            std::string pluginName;
            RE::FormID localFormID{ 0 };
        };

#pragma pack(push, 1)
        struct PersistedStateHeader
        {
            std::uint32_t count{ 0 };
            std::uint32_t reserved{ 0 };
            std::uint64_t nextEventID{ 1 };
        };

        struct PersistedPlacement
        {
            std::uint64_t eventID{ 0 };
            std::uint32_t baseLocalFormID{ 0 };
            char basePluginName[Protocol::kPluginNameCapacity]{};
            std::uint32_t cellLocalFormID{ 0 };
            char cellPluginName[Protocol::kPluginNameCapacity]{};
            float positionX{ 0.0f };
            float positionY{ 0.0f };
            float positionZ{ 0.0f };
            float angleX{ 0.0f };
            float angleY{ 0.0f };
            float angleZ{ 0.0f };
            std::uint8_t flags{ Protocol::kNone };
            std::uint8_t reserved[3]{};
        };
#pragma pack(pop)

        static_assert(sizeof(PersistedStateHeader) == 16);
        static_assert(sizeof(PersistedPlacement) == 564);

        [[nodiscard]] std::optional<FormIdentity> DescribeForm(RE::TESForm* form)
        {
            if (!form || form->GetFormID() == 0) {
                return std::nullopt;
            }

            auto* ownerFile = form->GetFile(0);
            if (!ownerFile || !ownerFile->fileName[0]) {
                return std::nullopt;
            }

            const auto pluginNameLength = std::strlen(ownerFile->fileName);
            if (pluginNameLength >= Protocol::kPluginNameCapacity) {
                return std::nullopt;
            }

            const auto localFormID = form->GetLocalFormID();
            if (localFormID == 0) {
                return std::nullopt;
            }

            return FormIdentity{ ownerFile->fileName, localFormID };
        }

        template <class T>
        [[nodiscard]] T* ResolveFormIdentity(std::string_view pluginName, RE::FormID localFormID)
        {
            if (pluginName.empty() || localFormID == 0) {
                return nullptr;
            }

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) {
                return nullptr;
            }

            auto* form = dataHandler->LookupForm(localFormID, pluginName);
            return form ? form->As<T>() : nullptr;
        }

        Protocol::Packet MakeObjectPacket(
            Protocol::PacketType type,
            std::uint64_t eventID,
            const FormIdentity& baseIdentity,
            const FormIdentity& cellIdentity,
            float px,
            float py,
            float pz,
            float ax,
            float ay,
            float az,
            bool isTent,
            std::uint64_t snapshotID = 0)
        {
            Protocol::Packet packet{};
            packet.type = type;
            packet.eventID = eventID;
            packet.snapshotID = snapshotID;
            packet.baseLocalFormID = baseIdentity.localFormID;
            std::memcpy(
                packet.basePluginName,
                baseIdentity.pluginName.c_str(),
                baseIdentity.pluginName.size() + 1);
            packet.cellLocalFormID = cellIdentity.localFormID;
            std::memcpy(
                packet.cellPluginName,
                cellIdentity.pluginName.c_str(),
                cellIdentity.pluginName.size() + 1);
            packet.flags = isTent ? Protocol::kTent : Protocol::kNone;
            if (snapshotID != 0) {
                packet.flags |= Protocol::kSnapshot;
            }
            packet.positionX = px;
            packet.positionY = py;
            packet.positionZ = pz;
            packet.angleX = ax;
            packet.angleY = ay;
            packet.angleZ = az;
            return packet;
        }

        Protocol::Packet MakeControlPacket(Protocol::PacketType type, std::uint64_t snapshotID)
        {
            Protocol::Packet packet{};
            packet.type = type;
            packet.snapshotID = snapshotID;
            return packet;
        }

        float DistanceSquared(float ax, float ay, float az, float bx, float by, float bz)
        {
            const float dx = ax - bx;
            const float dy = ay - by;
            const float dz = az - bz;
            return dx * dx + dy * dy + dz * dz;
        }

        [[nodiscard]] RE::TESObjectREFR* FindAnchorInCell(
            RE::TESObjectCELL* cell,
            STRPM::ConnectionID sender)
        {
            if (!cell || !cell->IsAttached()) {
                return nullptr;
            }

            if (auto* player = RE::PlayerCharacter::GetSingleton();
                player && player->GetParentCell() == cell) {
                return player;
            }

            if (const auto proxyID = STRPMClient::GetSingleton().ResolveProxy(sender)) {
                if (auto* proxy = RE::TESForm::LookupByID<RE::TESObjectREFR>(*proxyID);
                    proxy && proxy->GetParentCell() == cell) {
                    return proxy;
                }
            }

            RE::TESObjectREFR* anchor = nullptr;
            cell->ForEachReference([&anchor](RE::TESObjectREFR* reference) {
                if (reference && !reference->IsMarkedForDeletion()) {
                    anchor = reference;
                    return RE::BSContainer::ForEachResult::kStop;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            return anchor;
        }
    }

    CampfireSync& CampfireSync::GetSingleton()
    {
        static CampfireSync instance;
        return instance;
    }

    void CampfireSync::OnLocalPlaced(
        RE::TESObjectREFR* placedRef,
        float positionX,
        float positionY,
        float positionZ,
        float angleX,
        float angleY,
        float angleZ,
        bool isTent)
    {
        if (!placedRef) {
            return;
        }

        auto* base = placedRef->GetBaseObject();
        if (!base || base->GetFormID() == 0) {
            SKSE::log::warn("CFT LOCAL PLACE ignored: missing base object ref={:08X}", placedRef->GetFormID());
            return;
        }

        auto* parentCell = placedRef->GetParentCell();
        if (!parentCell) {
            parentCell = placedRef->GetSaveParentCell();
        }

        const auto baseIdentity = DescribeForm(base);
        const auto cellIdentity = DescribeForm(parentCell);
        if (!baseIdentity || !cellIdentity) {
            SKSE::log::warn(
                "CFT LOCAL PLACE ignored: identity unavailable ref={:08X} runtimeBase={:08X} cell={:08X}",
                placedRef->GetFormID(),
                base->GetFormID(),
                parentCell ? parentCell->GetFormID() : 0);
            return;
        }

        const auto eventID = _nextEventID.fetch_add(1);
        const auto runtimeBaseFormID = base->GetFormID();

        {
            std::scoped_lock lock(_mutex);
            _localPlacements.push_back({
                eventID,
                baseIdentity->pluginName,
                baseIdentity->localFormID,
                cellIdentity->pluginName,
                cellIdentity->localFormID,
                positionX,
                positionY,
                positionZ,
                angleX,
                angleY,
                angleZ,
                isTent
            });
        }

        SKSE::log::info(
            "CFT LOCAL PLACE ref={:08X} base={}:{:08X} runtimeBase={:08X} cell={}:{:08X} event={} tent={} pos=({:.2f},{:.2f},{:.2f}) rot=({:.2f},{:.2f},{:.2f})",
            placedRef->GetFormID(),
            baseIdentity->pluginName,
            baseIdentity->localFormID,
            runtimeBaseFormID,
            cellIdentity->pluginName,
            cellIdentity->localFormID,
            eventID,
            isTent ? 1 : 0,
            positionX,
            positionY,
            positionZ,
            angleX,
            angleY,
            angleZ);

        (void)STRPMClient::GetSingleton().Send(MakeObjectPacket(
            Protocol::PacketType::kPlace,
            eventID,
            *baseIdentity,
            *cellIdentity,
            positionX,
            positionY,
            positionZ,
            angleX,
            angleY,
            angleZ,
            isTent));
    }

    std::optional<CampfireSync::LocalPlacement> CampfireSync::TakeLocalPlacement(
        std::string_view pluginName,
        RE::FormID localFormID,
        float x,
        float y,
        float z,
        bool isTent)
    {
        std::scoped_lock lock(_mutex);

        auto best = _localPlacements.end();
        float bestDistance = kRemovalMatchRadius * kRemovalMatchRadius;
        for (auto it = _localPlacements.begin(); it != _localPlacements.end(); ++it) {
            if (it->localFormID != localFormID ||
                std::string_view(it->pluginName) != pluginName ||
                it->isTent != isTent) {
                continue;
            }

            const auto distance = DistanceSquared(it->x, it->y, it->z, x, y, z);
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = it;
            }
        }

        if (best == _localPlacements.end()) {
            return std::nullopt;
        }

        auto placement = *best;
        _localPlacements.erase(best);
        return placement;
    }

    bool CampfireSync::ConsumeSuppressedRemoval(RE::FormID baseFormID, float x, float y, float z, bool isTent)
    {
        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lock(_mutex);

        for (auto it = _suppressedRemovals.begin(); it != _suppressedRemovals.end();) {
            if (it->expiresAt <= now) {
                it = _suppressedRemovals.erase(it);
                continue;
            }

            if (it->baseFormID == baseFormID &&
                it->isTent == isTent &&
                DistanceSquared(it->x, it->y, it->z, x, y, z) <= kRemovalMatchRadius * kRemovalMatchRadius) {
                _suppressedRemovals.erase(it);
                return true;
            }

            ++it;
        }

        return false;
    }

    void CampfireSync::MarkSuppressedRemoval(const RemoteMirror& mirror)
    {
        float x = mirror.x;
        float y = mirror.y;
        float z = mirror.z;
        if (auto ref = mirror.handle.get()) {
            const auto& position = ref->data.location;
            x = position.x;
            y = position.y;
            z = position.z;
        }

        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lock(_mutex);

        while (!_suppressedRemovals.empty() && _suppressedRemovals.front().expiresAt <= now) {
            _suppressedRemovals.pop_front();
        }
        while (_suppressedRemovals.size() >= kMaxSuppressedRemovals) {
            _suppressedRemovals.pop_front();
        }

        _suppressedRemovals.push_back({
            mirror.runtimeBaseFormID,
            x,
            y,
            z,
            mirror.isTent,
            now + kRemovalSuppressionLifetime
        });
    }

    void CampfireSync::OnLocalRemoved(
        RE::TESForm* baseForm,
        float positionX,
        float positionY,
        float positionZ,
        float angleX,
        float angleY,
        float angleZ,
        bool isTent)
    {
        if (!baseForm || baseForm->GetFormID() == 0) {
            return;
        }

        const auto runtimeBaseFormID = baseForm->GetFormID();
        if (ConsumeSuppressedRemoval(runtimeBaseFormID, positionX, positionY, positionZ, isTent)) {
            SKSE::log::info(
                "CFT LOCAL REMOVE suppressed remote teardown runtimeBase={:08X} tent={} pos=({:.2f},{:.2f},{:.2f})",
                runtimeBaseFormID,
                isTent ? 1 : 0,
                positionX,
                positionY,
                positionZ);
            return;
        }

        const auto baseIdentity = DescribeForm(baseForm);
        if (!baseIdentity) {
            SKSE::log::warn(
                "CFT LOCAL REMOVE ignored: base identity unavailable runtimeBase={:08X}",
                runtimeBaseFormID);
            return;
        }

        const auto tracked = TakeLocalPlacement(
            baseIdentity->pluginName,
            baseIdentity->localFormID,
            positionX,
            positionY,
            positionZ,
            isTent);

        std::uint64_t eventID = 0;
        std::optional<FormIdentity> cellIdentity;
        if (tracked) {
            eventID = tracked->eventID;
            cellIdentity = FormIdentity{ tracked->cellPluginName, tracked->cellLocalFormID };
        } else if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            cellIdentity = DescribeForm(player->GetParentCell());
        }

        if (!cellIdentity) {
            SKSE::log::warn(
                "CFT LOCAL REMOVE ignored: cell identity unavailable base={}:{:08X} event={}",
                baseIdentity->pluginName,
                baseIdentity->localFormID,
                eventID);
            return;
        }

        SKSE::log::info(
            "CFT LOCAL REMOVE base={}:{:08X} runtimeBase={:08X} cell={}:{:08X} event={} tent={} pos=({:.2f},{:.2f},{:.2f})",
            baseIdentity->pluginName,
            baseIdentity->localFormID,
            runtimeBaseFormID,
            cellIdentity->pluginName,
            cellIdentity->localFormID,
            eventID,
            isTent ? 1 : 0,
            positionX,
            positionY,
            positionZ);

        (void)STRPMClient::GetSingleton().Send(MakeObjectPacket(
            Protocol::PacketType::kRemove,
            eventID,
            *baseIdentity,
            *cellIdentity,
            positionX,
            positionY,
            positionZ,
            angleX,
            angleY,
            angleZ,
            isTent));
    }

    void CampfireSync::HandleRemote(STRPM::ConnectionID sender, const Protocol::Packet& packet)
    {
        switch (packet.type) {
        case Protocol::PacketType::kPlace:
            StoreRemotePlacement(sender, packet);
            break;
        case Protocol::PacketType::kRemove:
            RemoveRemote(sender, packet);
            break;
        case Protocol::PacketType::kSnapshotRequest:
            SKSE::log::info("CFT SNAPSHOT REQUEST received connection={} request={}", sender, packet.snapshotID);
            SendSnapshot(sender);
            break;
        case Protocol::PacketType::kSnapshotBegin:
            BeginRemoteSnapshot(sender, packet.snapshotID);
            break;
        case Protocol::PacketType::kSnapshotEnd:
            EndRemoteSnapshot(sender, packet.snapshotID);
            break;
        default:
            break;
        }
    }

    void CampfireSync::OnPeerAvailable(STRPM::ConnectionID connectionID)
    {
        if (connectionID == 0) {
            return;
        }

        SKSE::log::info("CFT PEER available connection={} requesting/replaying state", connectionID);
        SendSnapshot(connectionID);
        STRPMClient::GetSingleton().RequestSnapshot(connectionID);
    }

    void CampfireSync::OnPeerUnavailable(STRPM::ConnectionID connectionID)
    {
        if (connectionID == 0) {
            return;
        }

        std::vector<RemoteMirror> mirrors;
        std::size_t stateRemoved = 0;
        {
            std::scoped_lock lock(_mutex);

            for (auto it = _remotePlacements.begin(); it != _remotePlacements.end();) {
                if (it->first.sender == connectionID) {
                    ++stateRemoved;
                    it = _remotePlacements.erase(it);
                } else {
                    ++it;
                }
            }

            for (auto it = _remoteMirrors.begin(); it != _remoteMirrors.end();) {
                if (it->first.sender == connectionID) {
                    mirrors.push_back(it->second);
                    it = _remoteMirrors.erase(it);
                } else {
                    ++it;
                }
            }
            _remoteSnapshots.erase(connectionID);
        }

        for (const auto& mirror : mirrors) {
            TeardownMirror(mirror);
        }

        SKSE::log::info(
            "CFT PEER unavailable connection={} stateRemoved={} mirrorsRemoved={}",
            connectionID,
            stateRemoved,
            mirrors.size());
    }

    void CampfireSync::OnAllPeersUnavailable()
    {
        std::vector<RemoteMirror> mirrors;
        std::size_t stateRemoved = 0;
        {
            std::scoped_lock lock(_mutex);
            stateRemoved = _remotePlacements.size();
            _remotePlacements.clear();
            mirrors.reserve(_remoteMirrors.size());
            for (const auto& entry : _remoteMirrors) {
                mirrors.push_back(entry.second);
            }
            _remoteMirrors.clear();
            _remoteSnapshots.clear();
        }

        for (const auto& mirror : mirrors) {
            TeardownMirror(mirror);
        }

        SKSE::log::info(
            "CFT PEERS cleared stateRemoved={} mirrorsRemoved={}",
            stateRemoved,
            mirrors.size());
    }

    void CampfireSync::OnCellFullyLoaded(RE::TESObjectCELL* cell)
    {
        const auto cellIdentity = DescribeForm(cell);
        if (!cellIdentity) {
            return;
        }

        std::vector<RemoteKey> pending;
        {
            std::scoped_lock lock(_mutex);
            for (const auto& entry : _remotePlacements) {
                if (entry.second.cellLocalFormID == cellIdentity->localFormID &&
                    entry.second.cellPluginName == cellIdentity->pluginName) {
                    pending.push_back(entry.first);
                }
            }
        }

        if (pending.empty()) {
            return;
        }

        SKSE::log::info(
            "CFT CELL loaded cell={}:{:08X} pendingState={}",
            cellIdentity->pluginName,
            cellIdentity->localFormID,
            pending.size());

        for (const auto& key : pending) {
            TryMaterializeRemote(key);
        }
    }

    void CampfireSync::BroadcastSnapshot()
    {
        SendSnapshot(std::nullopt);
    }

    void CampfireSync::SendSnapshot(std::optional<STRPM::ConnectionID> target)
    {
        std::deque<LocalPlacement> placements;
        {
            std::scoped_lock lock(_mutex);
            placements = _localPlacements;
        }

        const auto snapshotID = _nextSnapshotID.fetch_add(1);
        const auto sendPacket = [&](const Protocol::Packet& packet) {
            if (target) {
                return STRPMClient::GetSingleton().SendTo(*target, packet);
            }
            return STRPMClient::GetSingleton().Send(packet);
        };

        if (!sendPacket(MakeControlPacket(Protocol::PacketType::kSnapshotBegin, snapshotID))) {
            SKSE::log::debug(
                "CFT SNAPSHOT TX begin failed target={} id={} objects={}",
                target.value_or(0),
                snapshotID,
                placements.size());
            return;
        }

        for (const auto& placement : placements) {
            const FormIdentity baseIdentity{ placement.pluginName, placement.localFormID };
            const FormIdentity cellIdentity{ placement.cellPluginName, placement.cellLocalFormID };
            if (!sendPacket(MakeObjectPacket(
                    Protocol::PacketType::kPlace,
                    placement.eventID,
                    baseIdentity,
                    cellIdentity,
                    placement.x,
                    placement.y,
                    placement.z,
                    placement.angleX,
                    placement.angleY,
                    placement.angleZ,
                    placement.isTent,
                    snapshotID))) {
                SKSE::log::warn(
                    "CFT SNAPSHOT TX incomplete target={} id={} failedEvent={}",
                    target.value_or(0),
                    snapshotID,
                    placement.eventID);
                return;
            }
        }

        if (!sendPacket(MakeControlPacket(Protocol::PacketType::kSnapshotEnd, snapshotID))) {
            SKSE::log::warn(
                "CFT SNAPSHOT TX end failed target={} id={} objects={}",
                target.value_or(0),
                snapshotID,
                placements.size());
            return;
        }

        SKSE::log::info(
            "CFT SNAPSHOT TX complete target={} id={} objects={}",
            target.value_or(0),
            snapshotID,
            placements.size());
    }

    void CampfireSync::BeginRemoteSnapshot(STRPM::ConnectionID sender, std::uint64_t snapshotID)
    {
        RemoteSnapshotState state{};
        state.snapshotID = snapshotID;

        {
            std::scoped_lock lock(_mutex);
            for (const auto& entry : _remotePlacements) {
                if (entry.first.sender == sender && entry.first.eventID != 0) {
                    state.baselineEvents.insert(entry.first.eventID);
                }
            }
            _remoteSnapshots[sender] = std::move(state);
        }

        std::size_t baselineCount = 0;
        {
            std::scoped_lock lock(_mutex);
            if (const auto it = _remoteSnapshots.find(sender); it != _remoteSnapshots.end()) {
                baselineCount = it->second.baselineEvents.size();
            }
        }

        SKSE::log::info(
            "CFT SNAPSHOT RX begin connection={} id={} baseline={}",
            sender,
            snapshotID,
            baselineCount);
    }

    void CampfireSync::EndRemoteSnapshot(STRPM::ConnectionID sender, std::uint64_t snapshotID)
    {
        std::vector<RemoteMirror> staleMirrors;
        std::size_t seenCount = 0;
        std::size_t baselineCount = 0;
        std::size_t staleStateRemoved = 0;

        {
            std::scoped_lock lock(_mutex);
            const auto stateIt = _remoteSnapshots.find(sender);
            if (stateIt == _remoteSnapshots.end() || stateIt->second.snapshotID != snapshotID) {
                SKSE::log::warn(
                    "CFT SNAPSHOT RX end ignored connection={} id={} active={}",
                    sender,
                    snapshotID,
                    stateIt != _remoteSnapshots.end() ? stateIt->second.snapshotID : 0);
                return;
            }

            const auto& state = stateIt->second;
            seenCount = state.seenEvents.size();
            baselineCount = state.baselineEvents.size();

            for (const auto eventID : state.baselineEvents) {
                if (state.seenEvents.contains(eventID)) {
                    continue;
                }

                const RemoteKey key{ sender, eventID };
                staleStateRemoved += _remotePlacements.erase(key);
                if (const auto mirrorIt = _remoteMirrors.find(key); mirrorIt != _remoteMirrors.end()) {
                    staleMirrors.push_back(mirrorIt->second);
                    _remoteMirrors.erase(mirrorIt);
                }
            }

            _remoteSnapshots.erase(stateIt);
        }

        for (const auto& mirror : staleMirrors) {
            TeardownMirror(mirror);
        }

        SKSE::log::info(
            "CFT SNAPSHOT RX complete connection={} id={} baseline={} seen={} staleStateRemoved={} staleMirrorsRemoved={}",
            sender,
            snapshotID,
            baselineCount,
            seenCount,
            staleStateRemoved,
            staleMirrors.size());
    }

    void CampfireSync::StoreRemotePlacement(STRPM::ConnectionID sender, const Protocol::Packet& packet)
    {
        const RemoteKey key{ sender, packet.eventID };
        const RemotePlacement placement{
            packet.basePluginName,
            packet.baseLocalFormID,
            packet.cellPluginName,
            packet.cellLocalFormID,
            packet.positionX,
            packet.positionY,
            packet.positionZ,
            packet.angleX,
            packet.angleY,
            packet.angleZ,
            (packet.flags & Protocol::kTent) != 0
        };

        bool alreadyMaterialized = false;
        {
            std::scoped_lock lock(_mutex);

            if (packet.snapshotID != 0) {
                if (const auto stateIt = _remoteSnapshots.find(sender);
                    stateIt != _remoteSnapshots.end() && stateIt->second.snapshotID == packet.snapshotID) {
                    stateIt->second.seenEvents.insert(packet.eventID);
                }
            }

            _remotePlacements.insert_or_assign(key, placement);

            if (const auto mirrorIt = _remoteMirrors.find(key); mirrorIt != _remoteMirrors.end()) {
                if (mirrorIt->second.handle.get()) {
                    alreadyMaterialized = true;
                } else {
                    _remoteMirrors.erase(mirrorIt);
                }
            }
        }

        if (alreadyMaterialized) {
            SKSE::log::trace(
                "CFT REMOTE STATE duplicate/materialized connection={} event={} snapshot={}",
                sender,
                packet.eventID,
                packet.snapshotID);
            return;
        }

        SKSE::log::info(
            "CFT REMOTE STATE stored connection={} event={} snapshot={} base={}:{:08X} cell={}:{:08X}",
            sender,
            packet.eventID,
            packet.snapshotID,
            packet.basePluginName,
            packet.baseLocalFormID,
            packet.cellPluginName,
            packet.cellLocalFormID);

        TryMaterializeRemote(key);
    }

    void CampfireSync::TryMaterializeRemote(const RemoteKey& key)
    {
        RemotePlacement placement{};
        {
            std::scoped_lock lock(_mutex);
            const auto stateIt = _remotePlacements.find(key);
            if (stateIt == _remotePlacements.end()) {
                return;
            }
            placement = stateIt->second;

            if (const auto mirrorIt = _remoteMirrors.find(key); mirrorIt != _remoteMirrors.end()) {
                if (mirrorIt->second.handle.get()) {
                    return;
                }
                _remoteMirrors.erase(mirrorIt);
            }
        }

        auto* targetCell = ResolveFormIdentity<RE::TESObjectCELL>(
            placement.cellPluginName,
            placement.cellLocalFormID);
        if (!targetCell) {
            SKSE::log::warn(
                "CFT REMOTE MATERIALIZE failed unresolved cell connection={} event={} cell={}:{:08X}",
                key.sender,
                key.eventID,
                placement.cellPluginName,
                placement.cellLocalFormID);
            return;
        }

        if (!targetCell->IsAttached()) {
            SKSE::log::debug(
                "CFT REMOTE MATERIALIZE pending unloaded cell connection={} event={} cell={}:{:08X}",
                key.sender,
                key.eventID,
                placement.cellPluginName,
                placement.cellLocalFormID);
            return;
        }

        auto* base = ResolveFormIdentity<RE::TESBoundObject>(placement.pluginName, placement.localFormID);
        if (!base) {
            SKSE::log::warn(
                "CFT REMOTE MATERIALIZE failed unresolved base connection={} event={} base={}:{:08X}",
                key.sender,
                key.eventID,
                placement.pluginName,
                placement.localFormID);
            return;
        }

        auto* anchor = FindAnchorInCell(targetCell, key.sender);
        if (!anchor) {
            SKSE::log::warn(
                "CFT REMOTE MATERIALIZE pending no anchor connection={} event={} cell={}:{:08X}",
                key.sender,
                key.eventID,
                placement.cellPluginName,
                placement.cellLocalFormID);
            return;
        }

        auto mirror = anchor->PlaceObjectAtMe(base, false);
        if (!mirror) {
            SKSE::log::warn(
                "CFT REMOTE MATERIALIZE PlaceObjectAtMe failed connection={} event={} base={}:{:08X}",
                key.sender,
                key.eventID,
                placement.pluginName,
                placement.localFormID);
            return;
        }

        mirror->SetPosition(placement.x, placement.y, placement.z);
        mirror->data.angle = {
            RE::deg_to_rad(placement.angleX),
            RE::deg_to_rad(placement.angleY),
            RE::deg_to_rad(placement.angleZ)
        };
        mirror->Update3DPosition(true);

        const auto handle = mirror->CreateRefHandle();
        bool duplicate = false;
        {
            std::scoped_lock lock(_mutex);
            if (!_remotePlacements.contains(key)) {
                duplicate = true;
            } else if (const auto existing = _remoteMirrors.find(key);
                       existing != _remoteMirrors.end() && existing->second.handle.get()) {
                duplicate = true;
            } else {
                _remoteMirrors.insert_or_assign(key, RemoteMirror{
                    handle,
                    base->GetFormID(),
                    placement.pluginName,
                    placement.localFormID,
                    placement.cellPluginName,
                    placement.cellLocalFormID,
                    placement.x,
                    placement.y,
                    placement.z,
                    placement.isTent
                });
            }
        }

        if (duplicate) {
            DeleteMirror(handle);
            return;
        }

        SKSE::log::info(
            "CFT REMOTE PLACE created connection={} event={} mirror={:08X} base={}:{:08X} runtimeBase={:08X} cell={}:{:08X} tent={} pos=({:.2f},{:.2f},{:.2f})",
            key.sender,
            key.eventID,
            mirror->GetFormID(),
            placement.pluginName,
            placement.localFormID,
            base->GetFormID(),
            placement.cellPluginName,
            placement.cellLocalFormID,
            placement.isTent ? 1 : 0,
            placement.x,
            placement.y,
            placement.z);
    }

    bool CampfireSync::DispatchTakeDown(RE::ObjectRefHandle handle, const char* scriptName)
    {
        auto reference = handle.get();
        if (!reference || !scriptName || scriptName[0] == '\0') {
            return false;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            SKSE::log::warn("CFT REMOTE TEARDOWN failed: Papyrus VM unavailable");
            return false;
        }

        auto* policy = vm->GetObjectHandlePolicy();
        if (!policy) {
            SKSE::log::warn("CFT REMOTE TEARDOWN failed: object handle policy unavailable");
            return false;
        }

        const auto vmHandle = policy->GetHandleForObject(reference->GetFormType(), reference.get());
        if (vmHandle == policy->EmptyHandle()) {
            SKSE::log::warn(
                "CFT REMOTE TEARDOWN failed: no VM handle ref={:08X} script={}",
                reference->GetFormID(),
                scriptName);
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::Object> scriptObject;
        if (!vm->FindBoundObject(vmHandle, scriptName, scriptObject) || !scriptObject) {
            SKSE::log::warn(
                "CFT REMOTE TEARDOWN script not bound ref={:08X} script={}",
                reference->GetFormID(),
                scriptName);
            return false;
        }

        auto* args = RE::MakeFunctionArguments();
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        if (!vm->DispatchMethodCall(scriptObject, "TakeDown", args, callback)) {
            delete args;
            SKSE::log::warn(
                "CFT REMOTE TEARDOWN dispatch failed ref={:08X} script={}",
                reference->GetFormID(),
                scriptName);
            return false;
        }

        return true;
    }

    void CampfireSync::DeleteMirror(RE::ObjectRefHandle handle)
    {
        auto mirror = handle.get();
        if (!mirror) {
            return;
        }
        mirror->Disable();
        mirror->SetDelete(true);
    }

    void CampfireSync::TeardownMirror(const RemoteMirror& mirror)
    {
        auto reference = mirror.handle.get();
        if (!reference) {
            return;
        }

        MarkSuppressedRemoval(mirror);

        const char* scriptName = mirror.isTent ? "CampTent" : "CampCampfire";
        if (DispatchTakeDown(mirror.handle, scriptName)) {
            if (mirror.isTent) {
                SKSE::log::info(
                    "CFT REMOTE TENT TEARDOWN dispatched ref={:08X} base={}:{:08X} runtimeBase={:08X}",
                    reference->GetFormID(),
                    mirror.pluginName,
                    mirror.localFormID,
                    mirror.runtimeBaseFormID);
            } else {
                SKSE::log::info(
                    "CFT REMOTE CAMPFIRE TEARDOWN dispatched ref={:08X} base={}:{:08X} runtimeBase={:08X}",
                    reference->GetFormID(),
                    mirror.pluginName,
                    mirror.localFormID,
                    mirror.runtimeBaseFormID);
            }
            return;
        }

        DeleteMirror(mirror.handle);
        SKSE::log::info(
            "CFT REMOTE GENERIC TEARDOWN fallback ref={:08X} base={}:{:08X} runtimeBase={:08X} tent={}",
            reference->GetFormID(),
            mirror.pluginName,
            mirror.localFormID,
            mirror.runtimeBaseFormID,
            mirror.isTent ? 1 : 0);
    }

    void CampfireSync::RemoveRemote(STRPM::ConnectionID sender, const Protocol::Packet& packet)
    {
        std::optional<RemoteKey> matchedKey;
        RemoteMirror remoteMirror{};
        bool hasMirror = false;

        {
            std::scoped_lock lock(_mutex);

            if (packet.eventID != 0) {
                const RemoteKey key{ sender, packet.eventID };
                if (_remotePlacements.contains(key)) {
                    matchedKey = key;
                }
            }

            if (!matchedKey) {
                for (const auto& entry : _remotePlacements) {
                    if (entry.first.sender != sender ||
                        entry.second.localFormID != packet.baseLocalFormID ||
                        entry.second.pluginName != packet.basePluginName ||
                        entry.second.cellLocalFormID != packet.cellLocalFormID ||
                        entry.second.cellPluginName != packet.cellPluginName) {
                        continue;
                    }

                    if (DistanceSquared(
                            entry.second.x,
                            entry.second.y,
                            entry.second.z,
                            packet.positionX,
                            packet.positionY,
                            packet.positionZ) <= kRemovalMatchRadius * kRemovalMatchRadius) {
                        matchedKey = entry.first;
                        break;
                    }
                }
            }

            if (matchedKey) {
                _remotePlacements.erase(*matchedKey);
                if (const auto mirrorIt = _remoteMirrors.find(*matchedKey); mirrorIt != _remoteMirrors.end()) {
                    remoteMirror = mirrorIt->second;
                    _remoteMirrors.erase(mirrorIt);
                    hasMirror = true;
                }
            }
        }

        if (!matchedKey) {
            SKSE::log::warn(
                "CFT REMOTE REMOVE no state match connection={} event={} base={}:{:08X} cell={}:{:08X}",
                sender,
                packet.eventID,
                packet.basePluginName,
                packet.baseLocalFormID,
                packet.cellPluginName,
                packet.cellLocalFormID);
            return;
        }

        if (hasMirror) {
            TeardownMirror(remoteMirror);
        }

        SKSE::log::info(
            "CFT REMOTE REMOVE handled connection={} event={} materialized={} base={}:{:08X}",
            sender,
            matchedKey->eventID,
            hasMirror ? 1 : 0,
            packet.basePluginName,
            packet.baseLocalFormID);
    }

    bool CampfireSync::IsRemoteCampObject(RE::TESObjectREFR* reference) const
    {
        if (!reference) {
            return false;
        }

        const auto formID = reference->GetFormID();
        std::scoped_lock lock(_mutex);
        for (const auto& entry : _remoteMirrors) {
            const auto& mirror = entry.second;
            auto remoteRef = mirror.handle.get();
            if (remoteRef && remoteRef->GetFormID() == formID) {
                return true;
            }
        }
        return false;
    }

    void CampfireSync::SavePersistentState(SKSE::SerializationInterface* serialization) const
    {
        if (!serialization) {
            return;
        }

        std::deque<LocalPlacement> placements;
        {
            std::scoped_lock lock(_mutex);
            placements = _localPlacements;
        }

        if (placements.size() > kMaxPersistedPlacements) {
            SKSE::log::error(
                "CFT STATE SAVE refused unreasonable active object count={}",
                placements.size());
            return;
        }

        std::vector<PersistedPlacement> persisted;
        persisted.reserve(placements.size());
        for (const auto& placement : placements) {
            if (placement.eventID == 0 ||
                placement.localFormID == 0 ||
                placement.cellLocalFormID == 0 ||
                placement.pluginName.empty() ||
                placement.cellPluginName.empty() ||
                placement.pluginName.size() >= Protocol::kPluginNameCapacity ||
                placement.cellPluginName.size() >= Protocol::kPluginNameCapacity) {
                continue;
            }

            PersistedPlacement record{};
            record.eventID = placement.eventID;
            record.baseLocalFormID = placement.localFormID;
            std::memcpy(
                record.basePluginName,
                placement.pluginName.c_str(),
                placement.pluginName.size() + 1);
            record.cellLocalFormID = placement.cellLocalFormID;
            std::memcpy(
                record.cellPluginName,
                placement.cellPluginName.c_str(),
                placement.cellPluginName.size() + 1);
            record.positionX = placement.x;
            record.positionY = placement.y;
            record.positionZ = placement.z;
            record.angleX = placement.angleX;
            record.angleY = placement.angleY;
            record.angleZ = placement.angleZ;
            record.flags = placement.isTent ? Protocol::kTent : Protocol::kNone;
            persisted.push_back(record);
        }

        if (!serialization->OpenRecord(kStateRecordType, kStateRecordVersion)) {
            SKSE::log::error("CFT STATE SAVE failed: OpenRecord");
            return;
        }

        PersistedStateHeader header{};
        header.count = static_cast<std::uint32_t>(persisted.size());
        header.nextEventID = _nextEventID.load();

        if (!serialization->WriteRecordData(header)) {
            SKSE::log::error("CFT STATE SAVE failed: header");
            return;
        }

        for (const auto& record : persisted) {
            if (!serialization->WriteRecordData(record)) {
                SKSE::log::error("CFT STATE SAVE failed: event={}", record.eventID);
                return;
            }
        }

        SKSE::log::info(
            "CFT STATE SAVE objects={} nextEvent={}",
            persisted.size(),
            header.nextEventID);
    }

    void CampfireSync::LoadPersistentState(SKSE::SerializationInterface* serialization)
    {
        if (!serialization) {
            return;
        }

        std::deque<LocalPlacement> loadedPlacements;
        std::unordered_set<std::uint64_t> loadedEventIDs;
        std::uint64_t nextEventID = 1;
        std::uint64_t maxEventID = 0;
        bool foundState = false;

        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;
        while (serialization->GetNextRecordInfo(type, version, length)) {
            if (type != kStateRecordType) {
                continue;
            }

            foundState = true;
            if (version != kStateRecordVersion) {
                SKSE::log::warn(
                    "CFT STATE LOAD skipped unsupported record version={} bytes={}",
                    version,
                    length);
                continue;
            }

            PersistedStateHeader header{};
            if (serialization->ReadRecordData(header) != sizeof(header)) {
                SKSE::log::error("CFT STATE LOAD failed: truncated header");
                continue;
            }

            if (header.count > kMaxPersistedPlacements) {
                SKSE::log::error(
                    "CFT STATE LOAD rejected unreasonable count={}",
                    header.count);
                continue;
            }

            nextEventID = std::max<std::uint64_t>(nextEventID, header.nextEventID);

            for (std::uint32_t i = 0; i < header.count; ++i) {
                PersistedPlacement record{};
                if (serialization->ReadRecordData(record) != sizeof(record)) {
                    SKSE::log::error(
                        "CFT STATE LOAD truncated placement index={} count={}",
                        i,
                        header.count);
                    break;
                }

                if (record.eventID == 0 ||
                    record.baseLocalFormID == 0 ||
                    record.cellLocalFormID == 0 ||
                    record.basePluginName[0] == '\0' ||
                    record.cellPluginName[0] == '\0' ||
                    record.basePluginName[Protocol::kPluginNameCapacity - 1] != '\0' ||
                    record.cellPluginName[Protocol::kPluginNameCapacity - 1] != '\0' ||
                    loadedEventIDs.contains(record.eventID)) {
                    continue;
                }

                if (!ResolveFormIdentity<RE::TESBoundObject>(record.basePluginName, record.baseLocalFormID)) {
                    SKSE::log::warn(
                        "CFT STATE LOAD skipped unresolved base={}:{:08X} event={}",
                        record.basePluginName,
                        record.baseLocalFormID,
                        record.eventID);
                    continue;
                }

                if (!ResolveFormIdentity<RE::TESObjectCELL>(record.cellPluginName, record.cellLocalFormID)) {
                    SKSE::log::warn(
                        "CFT STATE LOAD skipped unresolved cell={}:{:08X} event={}",
                        record.cellPluginName,
                        record.cellLocalFormID,
                        record.eventID);
                    continue;
                }

                loadedEventIDs.insert(record.eventID);
                maxEventID = std::max(maxEventID, record.eventID);
                loadedPlacements.push_back({
                    record.eventID,
                    record.basePluginName,
                    record.baseLocalFormID,
                    record.cellPluginName,
                    record.cellLocalFormID,
                    record.positionX,
                    record.positionY,
                    record.positionZ,
                    record.angleX,
                    record.angleY,
                    record.angleZ,
                    (record.flags & Protocol::kTent) != 0
                });
            }
        }

        nextEventID = std::max(nextEventID, maxEventID + 1);
        if (nextEventID == 0) {
            nextEventID = 1;
        }

        {
            std::scoped_lock lock(_mutex);
            _localPlacements = std::move(loadedPlacements);
        }
        _nextEventID.store(nextEventID);

        SKSE::log::info(
            "CFT STATE LOAD objects={} nextEvent={} recordFound={}",
            loadedEventIDs.size(),
            nextEventID,
            foundState ? 1 : 0);
    }

    void CampfireSync::ClearPersistentState()
    {
        {
            std::scoped_lock lock(_mutex);
            _localPlacements.clear();
        }
        _nextEventID.store(1);
        SKSE::log::info("CFT STATE cleared local ownership registry");
    }

    void CampfireSync::ResetRemoteState()
    {
        OnAllPeersUnavailable();
        SKSE::log::info("CFT remote state reset");
    }

    void CampfireSync::Reset()
    {
        ResetRemoteState();
        ClearPersistentState();
        _nextSnapshotID.store(1);
        SKSE::log::info("CFT full state reset");
    }
}
