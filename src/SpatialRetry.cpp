#include "PCH.h"
#include "CampfireSync.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr float kExteriorCellSize = 4096.0f;

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
                    "CFT EXTERIOR GRID replaced premature mirror connection={} event={}",
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

            // Remote camps must not depend on a player/proxy remaining in the area.
            // Force persistence so the reference is fully owned by the local game once created.
            auto mirror = player->PlaceObjectAtMe(base, true);
            if (!mirror) {
                SKSE::log::warn(
                    "CFT EXTERIOR GRID PlaceObjectAtMe failed connection={} event={}",
                    key.sender,
                    key.eventID);
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

            auto* mirrorCell = mirror->GetParentCell();
            SKSE::log::info(
                "CFT EXTERIOR GRID PLACE created connection={} event={} mirror={:08X} grid=({},{}) tent={} pos=({:.2f},{:.2f},{:.2f}) parentCell={:08X} expectedCell={:08X} 3dLoaded={} persistent=1",
                key.sender,
                key.eventID,
                mirror->GetFormID(),
                loadedCoordinates->cellX,
                loadedCoordinates->cellY,
                placement.isTent ? 1 : 0,
                placement.x,
                placement.y,
                placement.z,
                mirrorCell ? mirrorCell->GetFormID() : 0,
                loadedCell->GetFormID(),
                mirror->Is3DLoaded() ? 1 : 0);
        }
    }
}
