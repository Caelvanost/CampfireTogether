#include "PCH.h"
#include "SharedCampSync.h"

#include "STRPMClient.h"

#include <random>

namespace CampfireTogether
{
    namespace
    {
        constexpr std::size_t kMaxSharedCamps = 100000;
        constexpr std::size_t kMaxSuppressedRemovals = 64;
        constexpr float kMatchRadius = 64.0f;
        constexpr auto kRemovalSuppressionLifetime = std::chrono::seconds(5);

        constexpr std::uint32_t kStateRecordType = 0x54415453;  // "STAT"
        constexpr std::uint32_t kStateRecordVersion = 3;
        constexpr std::uint32_t kLegacyStateRecordVersion = 2;

        struct FormIdentity
        {
            std::string pluginName;
            RE::FormID localFormID{ 0 };
        };

#pragma pack(push, 1)
        struct SharedStateHeader
        {
            std::uint32_t count{ 0 };
            std::uint32_t reserved{ 0 };
            std::uint64_t localNodeID{ 0 };
            std::uint64_t nextObjectID{ 1 };
        };

        struct PersistedSharedCamp
        {
            std::uint64_t originNodeID{ 0 };
            std::uint64_t objectID{ 0 };
            std::uint64_t revision{ 0 };
            std::uint64_t writerNodeID{ 0 };
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
            std::uint8_t reserved[7]{};
        };

        struct LegacyStateHeader
        {
            std::uint32_t count{ 0 };
            std::uint32_t reserved{ 0 };
            std::uint64_t nextEventID{ 1 };
        };

        struct LegacyPlacement
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

        static_assert(sizeof(SharedStateHeader) == 24);
        static_assert(sizeof(PersistedSharedCamp) == 592);
        static_assert(sizeof(LegacyStateHeader) == 16);
        static_assert(sizeof(LegacyPlacement) == 564);

        [[nodiscard]] std::optional<FormIdentity> DescribeForm(RE::TESForm* form)
        {
            if (!form || form->GetFormID() == 0) {
                return std::nullopt;
            }

            auto* ownerFile = form->GetFile(0);
            if (!ownerFile || !ownerFile->fileName[0]) {
                return std::nullopt;
            }

            const auto nameLength = std::strlen(ownerFile->fileName);
            if (nameLength >= Protocol::kPluginNameCapacity) {
                return std::nullopt;
            }

            const auto localFormID = form->GetLocalFormID();
            if (localFormID == 0) {
                return std::nullopt;
            }

            return FormIdentity{ ownerFile->fileName, localFormID };
        }

        template <class T>
        [[nodiscard]] T* ResolveForm(std::string_view pluginName, RE::FormID localFormID)
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

        [[nodiscard]] float DistanceSquared(
            float ax,
            float ay,
            float az,
            float bx,
            float by,
            float bz)
        {
            const float dx = ax - bx;
            const float dy = ay - by;
            const float dz = az - bz;
            return dx * dx + dy * dy + dz * dz;
        }

        [[nodiscard]] bool PositionInsideCell(const RE::EXTERIOR_DATA* coordinates, float x, float y)
        {
            if (!coordinates) {
                return false;
            }

            return x >= coordinates->worldX &&
                   x < coordinates->worldX + 4096.0f &&
                   y >= coordinates->worldY &&
                   y < coordinates->worldY + 4096.0f;
        }

        [[nodiscard]] bool IsPersistentExteriorCell(RE::TESObjectCELL* cell)
        {
            if (!cell || !cell->IsExteriorCell()) {
                return false;
            }

            auto* world = cell->GetRuntimeData().worldSpace;
            return world && world->persistentCell == cell;
        }

        [[nodiscard]] std::uint64_t GenerateNodeID()
        {
            std::random_device random;
            std::uint64_t value =
                (static_cast<std::uint64_t>(random()) << 32) ^
                static_cast<std::uint64_t>(random());
            value ^= static_cast<std::uint64_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
            value ^= static_cast<std::uint64_t>(::GetCurrentProcessId()) << 17;
            if (value == 0) {
                value = 1;
            }
            return value;
        }

        [[nodiscard]] Protocol::Packet MakeControlPacket(
            Protocol::PacketType type,
            std::uint64_t snapshotID)
        {
            Protocol::Packet packet{};
            packet.type = type;
            packet.snapshotID = snapshotID;
            return packet;
        }
    }

    SharedCampSync& SharedCampSync::GetSingleton()
    {
        static SharedCampSync instance;
        return instance;
    }

    std::uint64_t SharedCampSync::EnsureLocalNodeID()
    {
        std::scoped_lock lock(_mutex);
        if (_localNodeID == 0) {
            _localNodeID = GenerateNodeID();
            SKSE::log::info("CFT SHARED NODE generated node={:016X}", _localNodeID);
        }
        return _localNodeID;
    }

    SharedCampSync::CampID SharedCampSync::AllocateCampID()
    {
        const auto nodeID = EnsureLocalNodeID();
        std::scoped_lock lock(_mutex);
        auto objectID = _nextObjectID++;
        if (objectID == 0) {
            objectID = _nextObjectID++;
        }
        return CampID{ nodeID, objectID };
    }

    bool SharedCampSync::MergeRecord(const CampRecord& incoming, bool fromNetwork)
    {
        if (incoming.id.originNodeID == 0 ||
            incoming.id.objectID == 0 ||
            incoming.revision == 0 ||
            incoming.writerNodeID == 0) {
            return false;
        }

        bool changed = false;
        {
            std::scoped_lock lock(_mutex);
            const auto it = _sharedCamps.find(incoming.id);
            if (it == _sharedCamps.end()) {
                _sharedCamps.emplace(incoming.id, incoming);
                changed = true;
            } else {
                const auto& current = it->second;
                const bool newer =
                    incoming.revision > current.revision ||
                    (incoming.revision == current.revision && incoming.writerNodeID > current.writerNodeID);
                if (newer) {
                    it->second = incoming;
                    changed = true;
                }
            }
        }

        if (changed) {
            SKSE::log::info(
                "CFT SHARED MERGE origin={:016X} object={} rev={} writer={:016X} deleted={} source={}",
                incoming.id.originNodeID,
                incoming.id.objectID,
                incoming.revision,
                incoming.writerNodeID,
                incoming.deleted ? 1 : 0,
                fromNetwork ? "network" : "local");
        }
        return changed;
    }

    std::optional<SharedCampSync::CampID> SharedCampSync::FindActiveCamp(
        std::string_view pluginName,
        RE::FormID localFormID,
        float x,
        float y,
        float z,
        bool isTent) const
    {
        std::optional<CampID> best;
        float bestDistance = kMatchRadius * kMatchRadius;

        std::scoped_lock lock(_mutex);
        for (const auto& [id, record] : _sharedCamps) {
            if (record.deleted ||
                record.localFormID != localFormID ||
                record.pluginName != pluginName ||
                record.isTent != isTent) {
                continue;
            }

            const auto distance = DistanceSquared(record.x, record.y, record.z, x, y, z);
            if (distance <= bestDistance) {
                best = id;
                bestDistance = distance;
            }
        }
        return best;
    }

    void SharedCampSync::OnLocalPlaced(
        RE::TESObjectREFR* placedRef,
        float positionX,
        float positionY,
        float positionZ,
        float angleX,
        float angleY,
        float angleZ,
        bool isTent)
    {
        if (!placedRef || IsRemoteCampObject(placedRef)) {
            return;
        }

        auto* base = placedRef->GetBaseObject();
        auto* parentCell = placedRef->GetParentCell();
        if (!parentCell) {
            parentCell = placedRef->GetSaveParentCell();
        }

        const auto baseIdentity = DescribeForm(base);
        const auto cellIdentity = DescribeForm(parentCell);
        if (!baseIdentity || !cellIdentity) {
            SKSE::log::warn(
                "CFT SHARED CREATE ignored identity unavailable ref={:08X} base={:08X} cell={:08X}",
                placedRef->GetFormID(),
                base ? base->GetFormID() : 0,
                parentCell ? parentCell->GetFormID() : 0);
            return;
        }

        const auto id = AllocateCampID();
        CampRecord record{};
        record.id = id;
        record.revision = 1;
        record.writerNodeID = id.originNodeID;
        record.pluginName = baseIdentity->pluginName;
        record.localFormID = baseIdentity->localFormID;
        record.cellPluginName = cellIdentity->pluginName;
        record.cellLocalFormID = cellIdentity->localFormID;
        record.x = positionX;
        record.y = positionY;
        record.z = positionZ;
        record.angleX = angleX;
        record.angleY = angleY;
        record.angleZ = angleZ;
        record.isTent = isTent;
        record.deleted = false;

        if (!MergeRecord(record, false)) {
            return;
        }

        SKSE::log::info(
            "CFT SHARED CREATE origin={:016X} object={} rev=1 ref={:08X} base={}:{:08X} cell={}:{:08X} tent={} pos=({:.2f},{:.2f},{:.2f})",
            id.originNodeID,
            id.objectID,
            placedRef->GetFormID(),
            record.pluginName,
            record.localFormID,
            record.cellPluginName,
            record.cellLocalFormID,
            isTent ? 1 : 0,
            positionX,
            positionY,
            positionZ);

        BroadcastRecord(record);
    }

    bool SharedCampSync::ConsumeSuppressedRemoval(
        RE::FormID baseFormID,
        float x,
        float y,
        float z,
        bool isTent)
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
                DistanceSquared(it->x, it->y, it->z, x, y, z) <= kMatchRadius * kMatchRadius) {
                _suppressedRemovals.erase(it);
                return true;
            }
            ++it;
        }
        return false;
    }

    void SharedCampSync::OnLocalRemoved(
        RE::TESForm* baseForm,
        float positionX,
        float positionY,
        float positionZ,
        float,
        float,
        float,
        bool isTent)
    {
        if (!baseForm || baseForm->GetFormID() == 0) {
            return;
        }

        if (ConsumeSuppressedRemoval(
                baseForm->GetFormID(),
                positionX,
                positionY,
                positionZ,
                isTent)) {
            SKSE::log::info(
                "CFT SHARED REMOVE suppressed runtimeBase={:08X} tent={} pos=({:.2f},{:.2f},{:.2f})",
                baseForm->GetFormID(),
                isTent ? 1 : 0,
                positionX,
                positionY,
                positionZ);
            return;
        }

        const auto baseIdentity = DescribeForm(baseForm);
        if (!baseIdentity) {
            return;
        }

        const auto matched = FindActiveCamp(
            baseIdentity->pluginName,
            baseIdentity->localFormID,
            positionX,
            positionY,
            positionZ,
            isTent);
        if (!matched) {
            SKSE::log::warn(
                "CFT SHARED REMOVE no active match base={}:{:08X} tent={} pos=({:.2f},{:.2f},{:.2f})",
                baseIdentity->pluginName,
                baseIdentity->localFormID,
                isTent ? 1 : 0,
                positionX,
                positionY,
                positionZ);
            return;
        }

        CampRecord tombstone{};
        {
            std::scoped_lock lock(_mutex);
            const auto it = _sharedCamps.find(*matched);
            if (it == _sharedCamps.end() || it->second.deleted) {
                return;
            }
            tombstone = it->second;
        }

        tombstone.revision += 1;
        tombstone.writerNodeID = EnsureLocalNodeID();
        tombstone.deleted = true;

        Mirror removedMirror{};
        bool hadMirror = false;
        {
            std::scoped_lock lock(_mutex);
            _sharedCamps.insert_or_assign(*matched, tombstone);
            if (const auto mirrorIt = _mirrors.find(*matched); mirrorIt != _mirrors.end()) {
                removedMirror = mirrorIt->second;
                hadMirror = true;
                _mirrors.erase(mirrorIt);
            }
        }

        SKSE::log::info(
            "CFT SHARED TOMBSTONE origin={:016X} object={} rev={} writer={:016X} localRemoval=1 mirrorTracked={}",
            matched->originNodeID,
            matched->objectID,
            tombstone.revision,
            tombstone.writerNodeID,
            hadMirror ? 1 : 0);

        BroadcastRecord(tombstone);
    }

    void SharedCampSync::BroadcastRecord(const CampRecord& record)
    {
        Protocol::Packet packet{};
        packet.type = Protocol::PacketType::kState;
        packet.originNodeID = record.id.originNodeID;
        packet.objectID = record.id.objectID;
        packet.revision = record.revision;
        packet.writerNodeID = record.writerNodeID;
        packet.baseLocalFormID = record.localFormID;
        packet.cellLocalFormID = record.cellLocalFormID;
        packet.positionX = record.x;
        packet.positionY = record.y;
        packet.positionZ = record.z;
        packet.angleX = record.angleX;
        packet.angleY = record.angleY;
        packet.angleZ = record.angleZ;
        packet.flags = record.isTent ? Protocol::kTent : Protocol::kNone;
        if (record.deleted) {
            packet.flags |= Protocol::kDeleted;
        }
        std::memcpy(packet.basePluginName, record.pluginName.c_str(), record.pluginName.size() + 1);
        std::memcpy(packet.cellPluginName, record.cellPluginName.c_str(), record.cellPluginName.size() + 1);
        (void)STRPMClient::GetSingleton().Send(packet);
    }

    void SharedCampSync::HandleRemote(STRPM::ConnectionID sender, const Protocol::Packet& packet)
    {
        switch (packet.type) {
        case Protocol::PacketType::kState: {
            CampRecord incoming{};
            incoming.id = CampID{ packet.originNodeID, packet.objectID };
            incoming.revision = packet.revision;
            incoming.writerNodeID = packet.writerNodeID;
            incoming.pluginName = packet.basePluginName;
            incoming.localFormID = packet.baseLocalFormID;
            incoming.cellPluginName = packet.cellPluginName;
            incoming.cellLocalFormID = packet.cellLocalFormID;
            incoming.x = packet.positionX;
            incoming.y = packet.positionY;
            incoming.z = packet.positionZ;
            incoming.angleX = packet.angleX;
            incoming.angleY = packet.angleY;
            incoming.angleZ = packet.angleZ;
            incoming.isTent = (packet.flags & Protocol::kTent) != 0;
            incoming.deleted = (packet.flags & Protocol::kDeleted) != 0;

            if (packet.snapshotID != 0) {
                std::scoped_lock lock(_mutex);
                if (const auto it = _remoteSnapshots.find(sender);
                    it != _remoteSnapshots.end() && it->second.snapshotID == packet.snapshotID) {
                    ++it->second.seen;
                }
            }

            if (MergeRecord(incoming, true)) {
                ApplyRuntimeState(incoming.id);
            }
            break;
        }
        case Protocol::PacketType::kSnapshotRequest:
            SKSE::log::info(
                "CFT SHARED SNAPSHOT REQUEST received connection={} request={}",
                sender,
                packet.snapshotID);
            SendSnapshot(sender);
            break;
        case Protocol::PacketType::kSnapshotBegin: {
            std::scoped_lock lock(_mutex);
            _remoteSnapshots.insert_or_assign(sender, SnapshotReceiveState{ packet.snapshotID, 0 });
            SKSE::log::info(
                "CFT SHARED SNAPSHOT RX begin connection={} id={}",
                sender,
                packet.snapshotID);
            break;
        }
        case Protocol::PacketType::kSnapshotEnd: {
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
                "CFT SHARED SNAPSHOT RX complete connection={} id={} seen={}",
                sender,
                packet.snapshotID,
                seen);
            break;
        }
        default:
            break;
        }
    }

    void SharedCampSync::OnPeerAvailable(STRPM::ConnectionID connectionID)
    {
        if (connectionID == 0) {
            return;
        }
        SKSE::log::info(
            "CFT SHARED PEER available connection={} exchanging common registry",
            connectionID);
        SendSnapshot(connectionID);
        STRPMClient::GetSingleton().RequestSnapshot(connectionID);
    }

    void SharedCampSync::OnPeerUnavailable(STRPM::ConnectionID connectionID)
    {
        if (connectionID == 0) {
            return;
        }
        {
            std::scoped_lock lock(_mutex);
            _remoteSnapshots.erase(connectionID);
        }
        SKSE::log::info(
            "CFT SHARED PEER unavailable connection={} registryPreserved=1 mirrorsPreserved=1",
            connectionID);
    }

    void SharedCampSync::OnAllPeersUnavailable()
    {
        std::scoped_lock lock(_mutex);
        _remoteSnapshots.clear();
        SKSE::log::info("CFT SHARED peers cleared registryPreserved=1 mirrorsPreserved=1");
    }

    void SharedCampSync::BroadcastSnapshot()
    {
        SendSnapshot(std::nullopt);
    }

    void SharedCampSync::SendSnapshot(std::optional<STRPM::ConnectionID> target)
    {
        std::vector<CampRecord> records;
        {
            std::scoped_lock lock(_mutex);
            records.reserve(_sharedCamps.size());
            for (const auto& [id, record] : _sharedCamps) {
                (void)id;
                records.push_back(record);
            }
        }

        const auto snapshotID = _nextSnapshotID.fetch_add(1);
        const auto sendPacket = [&](const Protocol::Packet& packet) {
            return target ?
                STRPMClient::GetSingleton().SendTo(*target, packet) :
                STRPMClient::GetSingleton().Send(packet);
        };

        if (!sendPacket(MakeControlPacket(Protocol::PacketType::kSnapshotBegin, snapshotID))) {
            SKSE::log::debug(
                "CFT SHARED SNAPSHOT TX begin failed target={} id={} records={}",
                target.value_or(0),
                snapshotID,
                records.size());
            return;
        }

        std::size_t deleted = 0;
        for (const auto& record : records) {
            Protocol::Packet packet{};
            packet.type = Protocol::PacketType::kState;
            packet.originNodeID = record.id.originNodeID;
            packet.objectID = record.id.objectID;
            packet.revision = record.revision;
            packet.writerNodeID = record.writerNodeID;
            packet.snapshotID = snapshotID;
            packet.baseLocalFormID = record.localFormID;
            packet.cellLocalFormID = record.cellLocalFormID;
            packet.positionX = record.x;
            packet.positionY = record.y;
            packet.positionZ = record.z;
            packet.angleX = record.angleX;
            packet.angleY = record.angleY;
            packet.angleZ = record.angleZ;
            packet.flags = Protocol::kSnapshot | (record.isTent ? Protocol::kTent : Protocol::kNone);
            if (record.deleted) {
                packet.flags |= Protocol::kDeleted;
                ++deleted;
            }
            std::memcpy(packet.basePluginName, record.pluginName.c_str(), record.pluginName.size() + 1);
            std::memcpy(packet.cellPluginName, record.cellPluginName.c_str(), record.cellPluginName.size() + 1);

            if (!sendPacket(packet)) {
                SKSE::log::warn(
                    "CFT SHARED SNAPSHOT TX incomplete target={} id={} origin={:016X} object={}",
                    target.value_or(0),
                    snapshotID,
                    record.id.originNodeID,
                    record.id.objectID);
                return;
            }
        }

        if (!sendPacket(MakeControlPacket(Protocol::PacketType::kSnapshotEnd, snapshotID))) {
            SKSE::log::warn(
                "CFT SHARED SNAPSHOT TX end failed target={} id={}",
                target.value_or(0),
                snapshotID);
            return;
        }

        SKSE::log::info(
            "CFT SHARED SNAPSHOT TX complete target={} id={} records={} active={} tombstones={}",
            target.value_or(0),
            snapshotID,
            records.size(),
            records.size() - deleted,
            deleted);
    }

    RE::TESObjectCELL* SharedCampSync::FindLoadedCellForRecord(const CampRecord& record) const
    {
        auto* recordedCell = ResolveForm<RE::TESObjectCELL>(
            record.cellPluginName,
            record.cellLocalFormID);
        if (!recordedCell) {
            return nullptr;
        }

        if (recordedCell->IsInteriorCell()) {
            return recordedCell->IsAttached() ? recordedCell : nullptr;
        }

        auto* expectedWorld = recordedCell->GetRuntimeData().worldSpace;
        std::vector<RE::FormID> loadedIDs;
        {
            std::scoped_lock lock(_mutex);
            loadedIDs.assign(_loadedExteriorCells.begin(), _loadedExteriorCells.end());
        }

        for (const auto cellID : loadedIDs) {
            auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellID);
            if (!cell || !cell->IsExteriorCell() || !cell->IsAttached()) {
                continue;
            }

            if (IsPersistentExteriorCell(cell)) {
                continue;
            }

            if (expectedWorld && cell->GetRuntimeData().worldSpace != expectedWorld) {
                continue;
            }

            if (PositionInsideCell(cell->GetCoordinates(), record.x, record.y)) {
                SKSE::log::debug(
                    "CFT SHARED GRID resolved originCell={:08X} runtimeCell={:08X} pos=({:.2f},{:.2f})",
                    recordedCell->GetFormID(),
                    cell->GetFormID(),
                    record.x,
                    record.y);
                return cell;
            }
        }
        return nullptr;
    }

    RE::TESObjectREFR* SharedCampSync::FindAnchor(RE::TESObjectCELL* cell) const
    {
        if (!cell || !cell->IsAttached() || IsPersistentExteriorCell(cell)) {
            return nullptr;
        }

        if (auto* player = RE::PlayerCharacter::GetSingleton();
            player && player->GetParentCell() == cell) {
            return player;
        }

        RE::TESObjectREFR* anchor = nullptr;
        cell->ForEachReference([&anchor](RE::TESObjectREFR& reference) {
            if (!reference.IsMarkedForDeletion() && reference.GetFormID() != 0) {
                anchor = std::addressof(reference);
                return RE::BSContainer::ForEachResult::kStop;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return anchor;
    }

    RE::TESObjectREFR* SharedCampSync::FindExistingMirror(
        RE::TESObjectCELL* cell,
        const CampRecord& record) const
    {
        if (!cell || !cell->IsAttached() || IsPersistentExteriorCell(cell)) {
            return nullptr;
        }

        auto* expectedBase = ResolveForm<RE::TESBoundObject>(record.pluginName, record.localFormID);
        if (!expectedBase) {
            return nullptr;
        }

        RE::TESObjectREFR* match = nullptr;
        cell->ForEachReference([&](RE::TESObjectREFR& reference) {
            if (reference.IsMarkedForDeletion() ||
                reference.GetBaseObject() != expectedBase) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            const auto& position = reference.data.location;
            if (DistanceSquared(position.x, position.y, position.z, record.x, record.y, record.z) <=
                kMatchRadius * kMatchRadius) {
                match = std::addressof(reference);
                return RE::BSContainer::ForEachResult::kStop;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return match;
    }

    void SharedCampSync::MaterializeIfPossible(const CampID& id, const CampRecord& record)
    {
        if (record.deleted || id.originNodeID == EnsureLocalNodeID()) {
            return;
        }

        {
            std::scoped_lock lock(_mutex);
            if (const auto it = _mirrors.find(id); it != _mirrors.end()) {
                if (it->second.handle.get()) {
                    return;
                }
                _mirrors.erase(it);
            }
        }

        auto* targetCell = FindLoadedCellForRecord(record);
        if (!targetCell) {
            SKSE::log::debug(
                "CFT SHARED MATERIALIZE pending origin={:016X} object={} cell={}:{:08X}",
                id.originNodeID,
                id.objectID,
                record.cellPluginName,
                record.cellLocalFormID);
            return;
        }

        auto* base = ResolveForm<RE::TESBoundObject>(record.pluginName, record.localFormID);
        if (!base) {
            SKSE::log::warn(
                "CFT SHARED MATERIALIZE unresolved base origin={:016X} object={} base={}:{:08X}",
                id.originNodeID,
                id.objectID,
                record.pluginName,
                record.localFormID);
            return;
        }

        RE::TESObjectREFR* physical = FindExistingMirror(targetCell, record);
        bool adopted = physical != nullptr;
        RE::NiPointer<RE::TESObjectREFR> created;

        if (!physical) {
            auto* anchor = FindAnchor(targetCell);
            if (!anchor) {
                SKSE::log::debug(
                    "CFT SHARED MATERIALIZE pending no anchor origin={:016X} object={} cell={:08X}",
                    id.originNodeID,
                    id.objectID,
                    targetCell->GetFormID());
                return;
            }

            created = anchor->PlaceObjectAtMe(base, false);
            physical = created.get();
            if (!physical) {
                SKSE::log::warn(
                    "CFT SHARED MATERIALIZE PlaceObjectAtMe failed origin={:016X} object={} anchor={:08X}",
                    id.originNodeID,
                    id.objectID,
                    anchor->GetFormID());
                return;
            }

            if (physical->GetParentCell() != targetCell || IsPersistentExteriorCell(physical->GetParentCell())) {
                SKSE::log::warn(
                    "CFT SHARED MATERIALIZE rejected wrong parent origin={:016X} object={} ref={:08X} parent={:08X} expected={:08X}",
                    id.originNodeID,
                    id.objectID,
                    physical->GetFormID(),
                    physical->GetParentCell() ? physical->GetParentCell()->GetFormID() : 0,
                    targetCell->GetFormID());
                DeleteMirror(physical->CreateRefHandle());
                return;
            }

            physical->SetPosition(record.x, record.y, record.z);
            physical->data.angle = {
                RE::deg_to_rad(record.angleX),
                RE::deg_to_rad(record.angleY),
                RE::deg_to_rad(record.angleZ)
            };
            physical->Update3DPosition(true);
            if (!physical->Is3DLoaded()) {
                (void)physical->Load3D(false);
                physical->Update3DPosition(true);
            }
        }

        Mirror mirror{};
        mirror.handle = physical->CreateRefHandle();
        mirror.runtimeBaseFormID = base->GetFormID();
        mirror.pluginName = record.pluginName;
        mirror.localFormID = record.localFormID;
        mirror.x = record.x;
        mirror.y = record.y;
        mirror.z = record.z;
        mirror.isTent = record.isTent;

        bool accepted = false;
        {
            std::scoped_lock lock(_mutex);
            const auto stateIt = _sharedCamps.find(id);
            if (stateIt != _sharedCamps.end() &&
                !stateIt->second.deleted &&
                stateIt->second.revision == record.revision &&
                !_mirrors.contains(id)) {
                _mirrors.emplace(id, mirror);
                accepted = true;
            }
        }

        if (!accepted) {
            if (!adopted) {
                DeleteMirror(mirror.handle);
            }
            return;
        }

        SKSE::log::info(
            "CFT SHARED {} origin={:016X} object={} rev={} ref={:08X} cell={:08X} parent={:08X} tent={} 3dLoaded={} pos=({:.2f},{:.2f},{:.2f})",
            adopted ? "ADOPT" : "MATERIALIZE",
            id.originNodeID,
            id.objectID,
            record.revision,
            physical->GetFormID(),
            targetCell->GetFormID(),
            physical->GetParentCell() ? physical->GetParentCell()->GetFormID() : 0,
            record.isTent ? 1 : 0,
            physical->Is3DLoaded() ? 1 : 0,
            record.x,
            record.y,
            record.z);
    }

    void SharedCampSync::ApplyRuntimeState(const CampID& id)
    {
        CampRecord record{};
        Mirror mirror{};
        bool hadMirror = false;
        {
            std::scoped_lock lock(_mutex);
            const auto stateIt = _sharedCamps.find(id);
            if (stateIt == _sharedCamps.end()) {
                return;
            }
            record = stateIt->second;

            if (record.deleted) {
                if (const auto mirrorIt = _mirrors.find(id); mirrorIt != _mirrors.end()) {
                    mirror = mirrorIt->second;
                    hadMirror = true;
                    _mirrors.erase(mirrorIt);
                }
            }
        }

        if (record.deleted) {
            if (hadMirror) {
                TeardownMirror(mirror);
            }
            return;
        }

        MaterializeIfPossible(id, record);
    }

    void SharedCampSync::OnCellFullyLoaded(RE::TESObjectCELL* cell)
    {
        if (!cell) {
            return;
        }

        const bool persistentExterior = IsPersistentExteriorCell(cell);
        if (cell->IsExteriorCell() && !persistentExterior) {
            std::scoped_lock lock(_mutex);
            _loadedExteriorCells.insert(cell->GetFormID());
        } else if (persistentExterior) {
            SKSE::log::debug(
                "CFT SHARED CELL ignored persistent exterior cell={:08X}",
                cell->GetFormID());
        }

        const auto cellIdentity = DescribeForm(cell);
        const auto* coordinates = cell->IsExteriorCell() ? cell->GetCoordinates() : nullptr;
        auto* cellWorld = cell->IsExteriorCell() ? cell->GetRuntimeData().worldSpace : nullptr;

        std::vector<CampID> candidates;
        {
            std::scoped_lock lock(_mutex);
            candidates.reserve(_sharedCamps.size());
            for (const auto& [id, record] : _sharedCamps) {
                if (record.deleted || id.originNodeID == _localNodeID) {
                    continue;
                }

                bool match = false;
                if (cell->IsInteriorCell() &&
                    cellIdentity &&
                    record.cellLocalFormID == cellIdentity->localFormID &&
                    record.cellPluginName == cellIdentity->pluginName) {
                    match = true;
                } else if (cell->IsExteriorCell() && !persistentExterior && coordinates) {
                    auto* recordedCell = ResolveForm<RE::TESObjectCELL>(
                        record.cellPluginName,
                        record.cellLocalFormID);
                    auto* expectedWorld = recordedCell ? recordedCell->GetRuntimeData().worldSpace : nullptr;
                    match = (!expectedWorld || !cellWorld || expectedWorld == cellWorld) &&
                            PositionInsideCell(coordinates, record.x, record.y);
                }

                if (match) {
                    candidates.push_back(id);
                }
            }
        }

        if (!candidates.empty()) {
            SKSE::log::info(
                "CFT SHARED CELL loaded cell={:08X} exterior={} candidates={}",
                cell->GetFormID(),
                cell->IsExteriorCell() ? 1 : 0,
                candidates.size());
        }

        for (const auto& id : candidates) {
            ApplyRuntimeState(id);
        }
    }

    bool SharedCampSync::IsRemoteCampObject(RE::TESObjectREFR* reference) const
    {
        if (!reference) {
            return false;
        }

        const auto formID = reference->GetFormID();
        std::scoped_lock lock(_mutex);
        for (const auto& [id, mirror] : _mirrors) {
            (void)id;
            auto ref = mirror.handle.get();
            if (ref && ref->GetFormID() == formID) {
                return true;
            }
        }
        return false;
    }

    void SharedCampSync::MarkSuppressedRemoval(const Mirror& mirror)
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

    bool SharedCampSync::DispatchTakeDown(RE::ObjectRefHandle handle, const char* scriptName)
    {
        auto reference = handle.get();
        if (!reference || !scriptName || scriptName[0] == '\0') {
            return false;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            return false;
        }
        auto* policy = vm->GetObjectHandlePolicy();
        if (!policy) {
            return false;
        }

        const auto vmHandle = policy->GetHandleForObject(reference->GetFormType(), reference.get());
        if (vmHandle == policy->EmptyHandle()) {
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::Object> scriptObject;
        if (!vm->FindBoundObject(vmHandle, scriptName, scriptObject) || !scriptObject) {
            return false;
        }

        auto* args = RE::MakeFunctionArguments();
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        if (!vm->DispatchMethodCall(scriptObject, "TakeDown", args, callback)) {
            delete args;
            return false;
        }
        return true;
    }

    void SharedCampSync::DeleteMirror(RE::ObjectRefHandle handle)
    {
        auto mirror = handle.get();
        if (!mirror) {
            return;
        }
        mirror->Disable();
        mirror->SetDelete(true);
    }

    void SharedCampSync::TeardownMirror(const Mirror& mirror)
    {
        auto reference = mirror.handle.get();
        if (!reference) {
            return;
        }

        MarkSuppressedRemoval(mirror);
        const char* scriptName = mirror.isTent ? "CampTent" : "CampCampfire";
        if (DispatchTakeDown(mirror.handle, scriptName)) {
            SKSE::log::info(
                "CFT SHARED TEARDOWN dispatched ref={:08X} tent={} base={}:{:08X}",
                reference->GetFormID(),
                mirror.isTent ? 1 : 0,
                mirror.pluginName,
                mirror.localFormID);
            return;
        }

        DeleteMirror(mirror.handle);
        SKSE::log::info(
            "CFT SHARED TEARDOWN fallback ref={:08X} tent={} base={}:{:08X}",
            reference->GetFormID(),
            mirror.isTent ? 1 : 0,
            mirror.pluginName,
            mirror.localFormID);
    }

    void SharedCampSync::CompleteRemoteMaterialization(
        std::uint32_t,
        RE::TESObjectREFR* reference)
    {
        if (reference) {
            DeleteMirror(reference->CreateRefHandle());
            SKSE::log::debug(
                "CFT legacy Papyrus materialization completion discarded ref={:08X}",
                reference->GetFormID());
        }
    }

    void SharedCampSync::SavePersistentState(SKSE::SerializationInterface* serialization)
    {
        if (!serialization) {
            return;
        }

        const auto nodeID = EnsureLocalNodeID();
        std::vector<CampRecord> records;
        std::uint64_t nextObjectID = 1;
        {
            std::scoped_lock lock(_mutex);
            records.reserve(_sharedCamps.size());
            for (const auto& [id, record] : _sharedCamps) {
                (void)id;
                records.push_back(record);
            }
            nextObjectID = _nextObjectID;
        }

        if (records.size() > kMaxSharedCamps) {
            SKSE::log::error(
                "CFT SHARED STATE SAVE refused unreasonable count={}",
                records.size());
            return;
        }

        if (!serialization->OpenRecord(kStateRecordType, kStateRecordVersion)) {
            SKSE::log::error("CFT SHARED STATE SAVE failed OpenRecord");
            return;
        }

        SharedStateHeader header{};
        header.count = static_cast<std::uint32_t>(records.size());
        header.localNodeID = nodeID;
        header.nextObjectID = nextObjectID;
        if (!serialization->WriteRecordData(header)) {
            SKSE::log::error("CFT SHARED STATE SAVE failed header");
            return;
        }

        std::size_t tombstones = 0;
        for (const auto& record : records) {
            if (record.pluginName.empty() ||
                record.cellPluginName.empty() ||
                record.pluginName.size() >= Protocol::kPluginNameCapacity ||
                record.cellPluginName.size() >= Protocol::kPluginNameCapacity) {
                continue;
            }

            PersistedSharedCamp persisted{};
            persisted.originNodeID = record.id.originNodeID;
            persisted.objectID = record.id.objectID;
            persisted.revision = record.revision;
            persisted.writerNodeID = record.writerNodeID;
            persisted.baseLocalFormID = record.localFormID;
            persisted.cellLocalFormID = record.cellLocalFormID;
            persisted.positionX = record.x;
            persisted.positionY = record.y;
            persisted.positionZ = record.z;
            persisted.angleX = record.angleX;
            persisted.angleY = record.angleY;
            persisted.angleZ = record.angleZ;
            persisted.flags = record.isTent ? Protocol::kTent : Protocol::kNone;
            if (record.deleted) {
                persisted.flags |= Protocol::kDeleted;
                ++tombstones;
            }
            std::memcpy(
                persisted.basePluginName,
                record.pluginName.c_str(),
                record.pluginName.size() + 1);
            std::memcpy(
                persisted.cellPluginName,
                record.cellPluginName.c_str(),
                record.cellPluginName.size() + 1);

            if (!serialization->WriteRecordData(persisted)) {
                SKSE::log::error(
                    "CFT SHARED STATE SAVE failed origin={:016X} object={}",
                    record.id.originNodeID,
                    record.id.objectID);
                return;
            }
        }

        SKSE::log::info(
            "CFT SHARED STATE SAVE node={:016X} records={} active={} tombstones={} nextObject={}",
            nodeID,
            records.size(),
            records.size() - tombstones,
            tombstones,
            nextObjectID);
    }

    void SharedCampSync::LoadPersistentState(SKSE::SerializationInterface* serialization)
    {
        if (!serialization) {
            return;
        }

        std::unordered_map<CampID, CampRecord, CampIDHash> loaded;
        std::uint64_t loadedNodeID = 0;
        std::uint64_t nextObjectID = 1;
        bool foundState = false;
        bool migratedLegacy = false;

        const auto mergeLoaded = [&](const CampRecord& incoming) {
            const auto it = loaded.find(incoming.id);
            if (it == loaded.end() ||
                incoming.revision > it->second.revision ||
                (incoming.revision == it->second.revision &&
                 incoming.writerNodeID > it->second.writerNodeID)) {
                loaded.insert_or_assign(incoming.id, incoming);
            }
        };

        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;
        while (serialization->GetNextRecordInfo(type, version, length)) {
            if (type != kStateRecordType) {
                continue;
            }
            foundState = true;

            if (version == kStateRecordVersion) {
                SharedStateHeader header{};
                if (serialization->ReadRecordData(header) != sizeof(header)) {
                    SKSE::log::error("CFT SHARED STATE LOAD truncated header");
                    continue;
                }
                if (header.count > kMaxSharedCamps) {
                    SKSE::log::error(
                        "CFT SHARED STATE LOAD rejected count={}",
                        header.count);
                    continue;
                }

                loadedNodeID = header.localNodeID;
                nextObjectID = std::max<std::uint64_t>(1, header.nextObjectID);

                for (std::uint32_t i = 0; i < header.count; ++i) {
                    PersistedSharedCamp persisted{};
                    if (serialization->ReadRecordData(persisted) != sizeof(persisted)) {
                        SKSE::log::error(
                            "CFT SHARED STATE LOAD truncated record index={} count={}",
                            i,
                            header.count);
                        break;
                    }

                    if (persisted.originNodeID == 0 ||
                        persisted.objectID == 0 ||
                        persisted.revision == 0 ||
                        persisted.writerNodeID == 0 ||
                        persisted.baseLocalFormID == 0 ||
                        persisted.cellLocalFormID == 0 ||
                        persisted.basePluginName[0] == '\0' ||
                        persisted.cellPluginName[0] == '\0' ||
                        persisted.basePluginName[Protocol::kPluginNameCapacity - 1] != '\0' ||
                        persisted.cellPluginName[Protocol::kPluginNameCapacity - 1] != '\0') {
                        continue;
                    }

                    CampRecord record{};
                    record.id = CampID{ persisted.originNodeID, persisted.objectID };
                    record.revision = persisted.revision;
                    record.writerNodeID = persisted.writerNodeID;
                    record.pluginName = persisted.basePluginName;
                    record.localFormID = persisted.baseLocalFormID;
                    record.cellPluginName = persisted.cellPluginName;
                    record.cellLocalFormID = persisted.cellLocalFormID;
                    record.x = persisted.positionX;
                    record.y = persisted.positionY;
                    record.z = persisted.positionZ;
                    record.angleX = persisted.angleX;
                    record.angleY = persisted.angleY;
                    record.angleZ = persisted.angleZ;
                    record.isTent = (persisted.flags & Protocol::kTent) != 0;
                    record.deleted = (persisted.flags & Protocol::kDeleted) != 0;
                    mergeLoaded(record);
                }
            } else if (version == kLegacyStateRecordVersion) {
                LegacyStateHeader header{};
                if (serialization->ReadRecordData(header) != sizeof(header)) {
                    SKSE::log::error("CFT SHARED STATE MIGRATION truncated v2 header");
                    continue;
                }
                if (header.count > kMaxSharedCamps) {
                    continue;
                }

                if (loadedNodeID == 0) {
                    loadedNodeID = GenerateNodeID();
                }
                nextObjectID = std::max<std::uint64_t>(1, header.nextEventID);

                for (std::uint32_t i = 0; i < header.count; ++i) {
                    LegacyPlacement legacy{};
                    if (serialization->ReadRecordData(legacy) != sizeof(legacy)) {
                        break;
                    }
                    if (legacy.eventID == 0 ||
                        legacy.baseLocalFormID == 0 ||
                        legacy.cellLocalFormID == 0 ||
                        legacy.basePluginName[0] == '\0' ||
                        legacy.cellPluginName[0] == '\0') {
                        continue;
                    }

                    CampRecord record{};
                    record.id = CampID{ loadedNodeID, legacy.eventID };
                    record.revision = 1;
                    record.writerNodeID = loadedNodeID;
                    record.pluginName = legacy.basePluginName;
                    record.localFormID = legacy.baseLocalFormID;
                    record.cellPluginName = legacy.cellPluginName;
                    record.cellLocalFormID = legacy.cellLocalFormID;
                    record.x = legacy.positionX;
                    record.y = legacy.positionY;
                    record.z = legacy.positionZ;
                    record.angleX = legacy.angleX;
                    record.angleY = legacy.angleY;
                    record.angleZ = legacy.angleZ;
                    record.isTent = (legacy.flags & Protocol::kTent) != 0;
                    record.deleted = false;
                    mergeLoaded(record);
                    nextObjectID = std::max(nextObjectID, legacy.eventID + 1);
                    migratedLegacy = true;
                }
            } else {
                SKSE::log::warn(
                    "CFT SHARED STATE LOAD skipped unsupported version={} bytes={}",
                    version,
                    length);
            }
        }

        if (loadedNodeID == 0) {
            loadedNodeID = GenerateNodeID();
        }

        for (const auto& [id, record] : loaded) {
            (void)record;
            if (id.originNodeID == loadedNodeID) {
                nextObjectID = std::max(nextObjectID, id.objectID + 1);
            }
        }
        if (nextObjectID == 0) {
            nextObjectID = 1;
        }

        std::size_t tombstones = 0;
        for (const auto& [id, record] : loaded) {
            (void)id;
            if (record.deleted) {
                ++tombstones;
            }
        }

        {
            std::scoped_lock lock(_mutex);
            _sharedCamps = std::move(loaded);
            _mirrors.clear();
            _remoteSnapshots.clear();
            _loadedExteriorCells.clear();
            _suppressedRemovals.clear();
            _localNodeID = loadedNodeID;
            _nextObjectID = nextObjectID;
        }

        SKSE::log::info(
            "CFT SHARED STATE LOAD node={:016X} records={} active={} tombstones={} nextObject={} recordFound={} migratedV2={}",
            loadedNodeID,
            _sharedCamps.size(),
            _sharedCamps.size() - tombstones,
            tombstones,
            nextObjectID,
            foundState ? 1 : 0,
            migratedLegacy ? 1 : 0);
    }

    void SharedCampSync::ClearPersistentState()
    {
        std::scoped_lock lock(_mutex);
        _sharedCamps.clear();
        _mirrors.clear();
        _remoteSnapshots.clear();
        _loadedExteriorCells.clear();
        _suppressedRemovals.clear();
        _localNodeID = 0;
        _nextObjectID = 1;
        _nextSnapshotID.store(1);
        SKSE::log::info("CFT SHARED STATE cleared");
    }

    void SharedCampSync::ResetRuntimeState()
    {
        std::scoped_lock lock(_mutex);
        _mirrors.clear();
        _remoteSnapshots.clear();
        _loadedExteriorCells.clear();
        _suppressedRemovals.clear();
        SKSE::log::info(
            "CFT SHARED RUNTIME reset registryPreserved={} node={:016X}",
            _sharedCamps.size(),
            _localNodeID);
    }

    void SharedCampSync::Reset()
    {
        ClearPersistentState();
        (void)EnsureLocalNodeID();
        SKSE::log::info("CFT SHARED full reset newNode={:016X}", _localNodeID);
    }
}
