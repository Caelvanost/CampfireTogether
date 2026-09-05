#include "PCH.h"
#include "CampfireSync.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr float kExteriorCellSize = 4096.0f;
        constexpr auto kRemoteMaterializationTimeout = std::chrono::seconds(10);
        constexpr char kRemoteMaterializeEvent[] = "CFT_RemoteMaterialize";

        [[nodiscard]] std::int32_t WorldToCell(float value)
        {
            return static_cast<std::int32_t>(std::floor(value / kExteriorCellSize));
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

        [[nodiscard]] RE::TESObjectCELL* FindLoadedExteriorGridCell(
            RE::TES* tes,
            std::int32_t targetCellX,
            std::int32_t targetCellY)
        {
            if (!tes || !tes->gridCells || tes->gridCells->length == 0) {
                return nullptr;
            }

            const auto gridLength = tes->gridCells->length;
            for (std::uint32_t x = 0; x < gridLength; ++x) {
                for (std::uint32_t y = 0; y < gridLength; ++y) {
                    auto* cell = tes->gridCells->GetCell(x, y);
                    if (!cell || !cell->IsExteriorCell() || !cell->IsAttached()) {
                        continue;
                    }

                    const auto* coordinates = cell->GetCoordinates();
                    if (!coordinates ||
                        coordinates->cellX != targetCellX ||
                        coordinates->cellY != targetCellY) {
                        continue;
                    }

                    if (tes->worldSpace && cell->GetRuntimeData().worldSpace != tes->worldSpace) {
                        continue;
                    }

                    return cell;
                }
            }

            return nullptr;
        }

        [[nodiscard]] RE::TESObjectREFR* FindAnchorInLoadedGridCell(RE::TESObjectCELL* cell)
        {
            if (!cell || !cell->IsAttached()) {
                return nullptr;
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
    }

    void CampfireSync::RefreshRemoteExteriorAtPlayer()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* tes = RE::TES::GetSingleton();
        if (!player || !tes || tes->interiorCell) {
            return;
        }

        const auto& playerPosition = player->data.location;
        const auto playerCellX = WorldToCell(playerPosition.x);
        const auto playerCellY = WorldToCell(playerPosition.y);
        auto* loadedCell = FindLoadedExteriorGridCell(tes, playerCellX, playerCellY);
        if (!loadedCell) {
            SKSE::log::debug(
                "CFT EXTERIOR GRID pending player grid not loaded grid=({},{}) pos=({:.2f},{:.2f},{:.2f})",
                playerCellX,
                playerCellY,
                playerPosition.x,
                playerPosition.y,
                playerPosition.z);
            return;
        }

        SKSE::log::debug(
            "CFT EXTERIOR GRID refresh-at-player gridCell={:08X} grid=({},{}) pos=({:.2f},{:.2f},{:.2f})",
            loadedCell->GetFormID(),
            playerCellX,
            playerCellY,
            playerPosition.x,
            playerPosition.y,
            playerPosition.z);
        RefreshRemoteExteriorCell(loadedCell);
    }

    void CampfireSync::RefreshRemoteExteriorCell(RE::TESObjectCELL* loadedCell)
    {
        if (!loadedCell || !loadedCell->IsExteriorCell() || !loadedCell->IsAttached()) {
            return;
        }

        const auto* loadedCoordinates = loadedCell->GetCoordinates();
        if (!loadedCoordinates) {
            return;
        }

        auto* loadedWorld = loadedCell->GetRuntimeData().worldSpace;
        if (!loadedWorld) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* tes = RE::TES::GetSingleton();
        if (!player || !tes || tes->interiorCell) {
            return;
        }

        const auto& playerPosition = player->data.location;
        const auto playerCellX = WorldToCell(playerPosition.x);
        const auto playerCellY = WorldToCell(playerPosition.y);
        if (playerCellX != loadedCoordinates->cellX ||
            playerCellY != loadedCoordinates->cellY) {
            return;
        }

        if (tes->worldSpace && loadedWorld != tes->worldSpace) {
            return;
        }

        auto* confirmedGridCell = FindLoadedExteriorGridCell(tes, playerCellX, playerCellY);
        if (!confirmedGridCell || confirmedGridCell != loadedCell) {
            return;
        }

        std::vector<std::pair<RemoteKey, RemotePlacement>> gridCandidates;
        {
            std::scoped_lock lock(_mutex);
            gridCandidates.reserve(_remotePlacements.size());
            for (const auto& entry : _remotePlacements) {
                const auto& placement = entry.second;
                if (WorldToCell(placement.x) == loadedCoordinates->cellX &&
                    WorldToCell(placement.y) == loadedCoordinates->cellY) {
                    gridCandidates.emplace_back(entry.first, placement);
                }
            }
        }

        if (gridCandidates.empty()) {
            return;
        }

        std::vector<std::pair<RemoteKey, RemotePlacement>> candidates;
        candidates.reserve(gridCandidates.size());
        for (const auto& entry : gridCandidates) {
            const auto& placement = entry.second;
            auto* persistentCell = ResolveForm<RE::TESObjectCELL>(
                placement.cellPluginName,
                placement.cellLocalFormID);
            if (!persistentCell || !persistentCell->IsExteriorCell()) {
                continue;
            }

            if (persistentCell->GetRuntimeData().worldSpace != loadedWorld) {
                continue;
            }

            candidates.push_back(entry);
        }

        if (candidates.empty()) {
            return;
        }

        auto* anchor = FindAnchorInLoadedGridCell(loadedCell);
        if (!anchor) {
            SKSE::log::debug(
                "CFT EXTERIOR GRID pending no anchor gridCell={:08X} grid=({},{}) candidates={}",
                loadedCell->GetFormID(),
                loadedCoordinates->cellX,
                loadedCoordinates->cellY,
                candidates.size());
            return;
        }

        SKSE::log::info(
            "CFT EXTERIOR GRID player-present gridCell={:08X} grid=({},{}) anchor={:08X} candidates={}",
            loadedCell->GetFormID(),
            loadedCoordinates->cellX,
            loadedCoordinates->cellY,
            anchor->GetFormID(),
            candidates.size());

        for (const auto& [key, placement] : candidates) {
            RemoteMirror previous{};
            bool hadPrevious = false;
            bool alreadyValidated = false;

            {
                std::scoped_lock lock(_mutex);
                if (!_remotePlacements.contains(key)) {
                    continue;
                }

                if (const auto mirrorIt = _remoteMirrors.find(key); mirrorIt != _remoteMirrors.end()) {
                    if (mirrorIt->second.handle.get() && mirrorIt->second.spatiallyValidated) {
                        alreadyValidated = true;
                    } else {
                        previous = mirrorIt->second;
                        hadPrevious = true;
                        _remoteMirrors.erase(mirrorIt);
                    }
                }
            }

            if (alreadyValidated) {
                continue;
            }

            if (hadPrevious && previous.handle.get()) {
                TeardownMirror(previous);
                SKSE::log::debug(
                    "CFT EXTERIOR GRID tore down legacy/premature mirror connection={} event={}",
                    key.sender,
                    key.eventID);
            }

            auto* base = ResolveForm<RE::TESBoundObject>(placement.pluginName, placement.localFormID);
            if (!base) {
                SKSE::log::warn(
                    "CFT EXTERIOR GRID failed unresolved base connection={} event={} base={}:{:08X}",
                    key.sender,
                    key.eventID,
                    placement.pluginName,
                    placement.localFormID);
                continue;
            }

            auto mirror = anchor->PlaceObjectAtMe(base, false);
            if (!mirror) {
                SKSE::log::warn(
                    "CFT EXTERIOR GRID PlaceObjectAtMe failed connection={} event={} anchor={:08X}",
                    key.sender,
                    key.eventID,
                    anchor->GetFormID());
                continue;
            }

            auto* actualParentCell = mirror->GetParentCell();
            if (actualParentCell != loadedCell) {
                SKSE::log::warn(
                    "CFT EXTERIOR GRID rejected wrong parent connection={} event={} mirror={:08X} parentCell={:08X} expectedGridCell={:08X}",
                    key.sender,
                    key.eventID,
                    mirror->GetFormID(),
                    actualParentCell ? actualParentCell->GetFormID() : 0,
                    loadedCell->GetFormID());
                DeleteMirror(mirror->CreateRefHandle());
                continue;
            }

            mirror->SetPosition(placement.x, placement.y, placement.z);
            mirror->data.angle = {
                RE::deg_to_rad(placement.angleX),
                RE::deg_to_rad(placement.angleY),
                RE::deg_to_rad(placement.angleZ)
            };
            mirror->Update3DPosition(true);

            if (!mirror->Is3DLoaded()) {
                (void)mirror->Load3D(false);
                mirror->Update3DPosition(true);
            }

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
                        placement.isTent,
                        true
                    });
                }
            }

            if (duplicate) {
                DeleteMirror(handle);
                continue;
            }

            SKSE::log::info(
                "CFT EXTERIOR GRID PLACE created connection={} event={} mirror={:08X} gridCell={:08X} parentCell={:08X} tent={} 3dLoaded={} pos=({:.2f},{:.2f},{:.2f})",
                key.sender,
                key.eventID,
                mirror->GetFormID(),
                loadedCell->GetFormID(),
                actualParentCell->GetFormID(),
                placement.isTent ? 1 : 0,
                mirror->Is3DLoaded() ? 1 : 0,
                placement.x,
                placement.y,
                placement.z);
        }
    }

    void CampfireSync::RequestRemoteMaterialization(
        const RemoteKey& key,
        const RemotePlacement& placement,
        RE::TESObjectCELL* expectedCell)
    {
        if (!expectedCell || !expectedCell->IsExteriorCell()) {
            return;
        }

        auto* base = ResolveForm<RE::TESBoundObject>(placement.pluginName, placement.localFormID);
        if (!base) {
            SKSE::log::warn(
                "CFT PAPYRUS MATERIALIZE request failed unresolved base connection={} event={} base={}:{:08X}",
                key.sender,
                key.eventID,
                placement.pluginName,
                placement.localFormID);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        std::uint32_t requestID = 0;
        {
            std::scoped_lock lock(_mutex);

            if (!_remotePlacements.contains(key)) {
                return;
            }

            if (const auto mirrorIt = _remoteMirrors.find(key);
                mirrorIt != _remoteMirrors.end() && mirrorIt->second.handle.get() && mirrorIt->second.spatiallyValidated) {
                return;
            }

            for (auto it = _remoteMaterializationRequests.begin(); it != _remoteMaterializationRequests.end();) {
                if (now - it->second.createdAt >= kRemoteMaterializationTimeout) {
                    SKSE::log::warn(
                        "CFT PAPYRUS MATERIALIZE request expired request={} connection={} event={}",
                        it->first,
                        it->second.key.sender,
                        it->second.key.eventID);
                    it = _remoteMaterializationRequests.erase(it);
                } else {
                    ++it;
                }
            }

            for (const auto& entry : _remoteMaterializationRequests) {
                if (entry.second.key == key) {
                    return;
                }
            }

            requestID = _nextRemoteMaterializationRequestID.fetch_add(1);
            if (requestID == 0 || requestID > 8000000) {
                _nextRemoteMaterializationRequestID.store(2);
                requestID = 1;
            }

            _remoteMaterializationRequests.insert_or_assign(
                requestID,
                RemoteMaterializationRequest{
                    key,
                    placement,
                    expectedCell->GetFormID(),
                    now
                });
        }

        auto* source = SKSE::GetModCallbackEventSource();
        if (!source) {
            {
                std::scoped_lock lock(_mutex);
                _remoteMaterializationRequests.erase(requestID);
            }
            SKSE::log::warn(
                "CFT PAPYRUS MATERIALIZE request failed no ModCallbackEvent source request={} connection={} event={}",
                requestID,
                key.sender,
                key.eventID);
            return;
        }

        const SKSE::ModCallbackEvent event{
            kRemoteMaterializeEvent,
            {},
            static_cast<float>(requestID),
            base
        };
        source->SendEvent(&event);

        SKSE::log::info(
            "CFT PAPYRUS MATERIALIZE request={} connection={} event={} base={}:{:08X} expectedCell={:08X} pos=({:.2f},{:.2f},{:.2f})",
            requestID,
            key.sender,
            key.eventID,
            placement.pluginName,
            placement.localFormID,
            expectedCell->GetFormID(),
            placement.x,
            placement.y,
            placement.z);
    }

    bool CampfireSync::IsRemoteMaterializationRequestValid(std::uint32_t requestID) const
    {
        if (requestID == 0) {
            return false;
        }
        std::scoped_lock lock(_mutex);
        return _remoteMaterializationRequests.contains(requestID);
    }

    float CampfireSync::GetRemoteMaterializationX(std::uint32_t requestID) const
    {
        std::scoped_lock lock(_mutex);
        const auto it = _remoteMaterializationRequests.find(requestID);
        return it != _remoteMaterializationRequests.end() ? it->second.placement.x : 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationY(std::uint32_t requestID) const
    {
        std::scoped_lock lock(_mutex);
        const auto it = _remoteMaterializationRequests.find(requestID);
        return it != _remoteMaterializationRequests.end() ? it->second.placement.y : 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationZ(std::uint32_t requestID) const
    {
        std::scoped_lock lock(_mutex);
        const auto it = _remoteMaterializationRequests.find(requestID);
        return it != _remoteMaterializationRequests.end() ? it->second.placement.z : 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationAngleX(std::uint32_t requestID) const
    {
        std::scoped_lock lock(_mutex);
        const auto it = _remoteMaterializationRequests.find(requestID);
        return it != _remoteMaterializationRequests.end() ? it->second.placement.angleX : 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationAngleY(std::uint32_t requestID) const
    {
        std::scoped_lock lock(_mutex);
        const auto it = _remoteMaterializationRequests.find(requestID);
        return it != _remoteMaterializationRequests.end() ? it->second.placement.angleY : 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationAngleZ(std::uint32_t requestID) const
    {
        std::scoped_lock lock(_mutex);
        const auto it = _remoteMaterializationRequests.find(requestID);
        return it != _remoteMaterializationRequests.end() ? it->second.placement.angleZ : 0.0f;
    }

    void CampfireSync::CompleteRemoteMaterialization(
        std::uint32_t requestID,
        RE::TESObjectREFR* reference)
    {
        RemoteMaterializationRequest request{};
        bool found = false;
        {
            std::scoped_lock lock(_mutex);
            const auto it = _remoteMaterializationRequests.find(requestID);
            if (it != _remoteMaterializationRequests.end()) {
                request = it->second;
                _remoteMaterializationRequests.erase(it);
                found = true;
            }
        }

        if (!found) {
            if (reference) {
                DeleteMirror(reference->CreateRefHandle());
            }
            SKSE::log::warn("CFT PAPYRUS MATERIALIZE completion ignored unknown request={}", requestID);
            return;
        }

        if (!reference) {
            SKSE::log::warn(
                "CFT PAPYRUS MATERIALIZE completion missing reference request={} connection={} event={}",
                requestID,
                request.key.sender,
                request.key.eventID);
            return;
        }

        auto* expectedCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(request.expectedCellFormID);
        auto* actualCell = reference->GetParentCell();
        auto* expectedBase = ResolveForm<RE::TESBoundObject>(
            request.placement.pluginName,
            request.placement.localFormID);
        auto* actualBase = reference->GetBaseObject();

        if (!expectedCell || actualCell != expectedCell || !expectedBase || actualBase != expectedBase) {
            SKSE::log::warn(
                "CFT PAPYRUS MATERIALIZE rejected request={} connection={} event={} ref={:08X} parentCell={:08X} expectedCell={:08X} actualBase={:08X} expectedBase={:08X}",
                requestID,
                request.key.sender,
                request.key.eventID,
                reference->GetFormID(),
                actualCell ? actualCell->GetFormID() : 0,
                expectedCell ? expectedCell->GetFormID() : 0,
                actualBase ? actualBase->GetFormID() : 0,
                expectedBase ? expectedBase->GetFormID() : 0);
            DeleteMirror(reference->CreateRefHandle());
            return;
        }

        const auto handle = reference->CreateRefHandle();
        bool duplicate = false;
        {
            std::scoped_lock lock(_mutex);
            if (!_remotePlacements.contains(request.key)) {
                duplicate = true;
            } else if (const auto existing = _remoteMirrors.find(request.key);
                       existing != _remoteMirrors.end() && existing->second.handle.get()) {
                duplicate = true;
            } else {
                _remoteMirrors.insert_or_assign(request.key, RemoteMirror{
                    handle,
                    expectedBase->GetFormID(),
                    request.placement.pluginName,
                    request.placement.localFormID,
                    request.placement.cellPluginName,
                    request.placement.cellLocalFormID,
                    request.placement.x,
                    request.placement.y,
                    request.placement.z,
                    request.placement.isTent,
                    true
                });
            }
        }

        if (duplicate) {
            DeleteMirror(handle);
            return;
        }

        SKSE::log::info(
            "CFT PAPYRUS MATERIALIZE complete request={} connection={} event={} ref={:08X} parentCell={:08X} 3dLoaded={} tent={}",
            requestID,
            request.key.sender,
            request.key.eventID,
            reference->GetFormID(),
            actualCell->GetFormID(),
            reference->Is3DLoaded() ? 1 : 0,
            request.placement.isTent ? 1 : 0);
    }

    void CampfireSync::FailRemoteMaterialization(std::uint32_t requestID)
    {
        RemoteMaterializationRequest request{};
        bool found = false;
        {
            std::scoped_lock lock(_mutex);
            const auto it = _remoteMaterializationRequests.find(requestID);
            if (it != _remoteMaterializationRequests.end()) {
                request = it->second;
                _remoteMaterializationRequests.erase(it);
                found = true;
            }
        }

        if (found) {
            SKSE::log::warn(
                "CFT PAPYRUS MATERIALIZE failed request={} connection={} event={}",
                requestID,
                request.key.sender,
                request.key.eventID);
        }
    }
}
