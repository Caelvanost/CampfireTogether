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
    }

    void CampfireSync::RefreshRemoteExteriorAtPlayer()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* tes = RE::TES::GetSingleton();
        if (!player || !tes) {
            return;
        }

        const auto& playerPosition = player->data.location;
        auto* loadedCell = tes->GetCell(playerPosition);
        if (!loadedCell || !loadedCell->IsExteriorCell()) {
            return;
        }

        SKSE::log::debug(
            "CFT EXTERIOR GRID refresh-at-player cell={:08X} pos=({:.2f},{:.2f},{:.2f})",
            loadedCell->GetFormID(),
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
        if (!player) {
            return;
        }

        const auto& playerPosition = player->data.location;
        const auto playerCellX = WorldToCell(playerPosition.x);
        const auto playerCellY = WorldToCell(playerPosition.y);

        auto* tes = RE::TES::GetSingleton();
        auto* playerGridCell = tes ? tes->GetCell(playerPosition) : nullptr;
        if (!playerGridCell || playerGridCell != loadedCell) {
            return;
        }

        if (playerCellX != loadedCoordinates->cellX ||
            playerCellY != loadedCoordinates->cellY) {
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

        SKSE::log::info(
            "CFT EXTERIOR GRID player-present cell={:08X} grid=({},{}) candidates={}",
            loadedCell->GetFormID(),
            loadedCoordinates->cellX,
            loadedCoordinates->cellY,
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
                DeleteMirror(previous.handle);
                SKSE::log::debug(
                    "CFT EXTERIOR GRID removed legacy/premature mirror connection={} event={}",
                    key.sender,
                    key.eventID);
            }

            RequestRemoteMaterialization(key, placement, loadedCell);
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
