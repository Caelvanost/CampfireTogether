#include "PCH.h"
#include "CampfireSync.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr float kExteriorCellSize = 4096.0f;

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

        [[nodiscard]] bool PositionInsideExteriorCell(
            const RE::EXTERIOR_DATA* coordinates,
            float x,
            float y)
        {
            if (!coordinates) {
                return false;
            }

            return x >= coordinates->worldX &&
                   x < coordinates->worldX + kExteriorCellSize &&
                   y >= coordinates->worldY &&
                   y < coordinates->worldY + kExteriorCellSize;
        }

        [[nodiscard]] RE::TESObjectCELL* FindLoadedExteriorCellAtPosition(
            RE::TES* tes,
            float x,
            float y)
        {
            if (!tes || !tes->gridCells || tes->gridCells->length == 0) {
                return nullptr;
            }

            const auto gridLength = tes->gridCells->length;
            for (std::uint32_t gridX = 0; gridX < gridLength; ++gridX) {
                for (std::uint32_t gridY = 0; gridY < gridLength; ++gridY) {
                    auto* cell = tes->gridCells->GetCell(gridX, gridY);
                    if (!cell || !cell->IsExteriorCell() || !cell->IsAttached()) {
                        continue;
                    }

                    if (tes->worldSpace && cell->GetRuntimeData().worldSpace != tes->worldSpace) {
                        continue;
                    }

                    if (PositionInsideExteriorCell(cell->GetCoordinates(), x, y)) {
                        return cell;
                    }
                }
            }

            return nullptr;
        }

        [[nodiscard]] RE::TESObjectREFR* FindAnchorInLoadedExteriorCell(RE::TESObjectCELL* cell)
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
        auto* loadedCell = FindLoadedExteriorCellAtPosition(
            tes,
            playerPosition.x,
            playerPosition.y);
        if (!loadedCell) {
            SKSE::log::debug(
                "CFT EXTERIOR BOUNDS pending player cell not found pos=({:.2f},{:.2f},{:.2f}) gridLength={}",
                playerPosition.x,
                playerPosition.y,
                playerPosition.z,
                tes->gridCells ? tes->gridCells->length : 0);
            return;
        }

        const auto* coordinates = loadedCell->GetCoordinates();
        SKSE::log::debug(
            "CFT EXTERIOR BOUNDS refresh-at-player cell={:08X} cellXY=({},{}) worldOrigin=({:.2f},{:.2f}) pos=({:.2f},{:.2f},{:.2f})",
            loadedCell->GetFormID(),
            coordinates ? coordinates->cellX : 0,
            coordinates ? coordinates->cellY : 0,
            coordinates ? coordinates->worldX : 0.0f,
            coordinates ? coordinates->worldY : 0.0f,
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

        const auto* coordinates = loadedCell->GetCoordinates();
        if (!coordinates) {
            return;
        }

        auto* loadedWorld = loadedCell->GetRuntimeData().worldSpace;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* tes = RE::TES::GetSingleton();
        if (!loadedWorld || !player || !tes || tes->interiorCell) {
            return;
        }

        if (tes->worldSpace && loadedWorld != tes->worldSpace) {
            return;
        }

        const auto& playerPosition = player->data.location;
        if (!PositionInsideExteriorCell(
                coordinates,
                playerPosition.x,
                playerPosition.y)) {
            return;
        }

        std::vector<std::pair<RemoteKey, RemotePlacement>> candidates;
        {
            std::scoped_lock lock(_mutex);
            candidates.reserve(_remotePlacements.size());
            for (const auto& entry : _remotePlacements) {
                const auto& placement = entry.second;
                if (!PositionInsideExteriorCell(coordinates, placement.x, placement.y)) {
                    continue;
                }

                auto* persistentCell = ResolveForm<RE::TESObjectCELL>(
                    placement.cellPluginName,
                    placement.cellLocalFormID);
                if (!persistentCell || !persistentCell->IsExteriorCell()) {
                    continue;
                }

                if (persistentCell->GetRuntimeData().worldSpace != loadedWorld) {
                    continue;
                }

                candidates.emplace_back(entry.first, placement);
            }
        }

        if (candidates.empty()) {
            return;
        }

        auto* anchor = FindAnchorInLoadedExteriorCell(loadedCell);
        if (!anchor) {
            SKSE::log::debug(
                "CFT EXTERIOR BOUNDS pending no anchor cell={:08X} cellXY=({},{}) worldOrigin=({:.2f},{:.2f}) candidates={}",
                loadedCell->GetFormID(),
                coordinates->cellX,
                coordinates->cellY,
                coordinates->worldX,
                coordinates->worldY,
                candidates.size());
            return;
        }

        SKSE::log::info(
            "CFT EXTERIOR BOUNDS player-present cell={:08X} cellXY=({},{}) worldOrigin=({:.2f},{:.2f}) anchor={:08X} candidates={}",
            loadedCell->GetFormID(),
            coordinates->cellX,
            coordinates->cellY,
            coordinates->worldX,
            coordinates->worldY,
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

                if (const auto mirrorIt = _remoteMirrors.find(key);
                    mirrorIt != _remoteMirrors.end()) {
                    if (mirrorIt->second.handle.get() &&
                        mirrorIt->second.spatiallyValidated &&
                        mirrorIt->second.handle.get()->GetParentCell() == loadedCell) {
                        alreadyValidated = true;
                    } else {
                        previous = mirrorIt->second;
                        hadPrevious = mirrorIt->second.handle.get() != nullptr;
                        _remoteMirrors.erase(mirrorIt);
                    }
                }
            }

            if (alreadyValidated) {
                continue;
            }

            if (hadPrevious) {
                TeardownMirror(previous);
                SKSE::log::debug(
                    "CFT EXTERIOR BOUNDS tore down premature mirror connection={} event={}",
                    key.sender,
                    key.eventID);
            }

            auto* base = ResolveForm<RE::TESBoundObject>(
                placement.pluginName,
                placement.localFormID);
            if (!base) {
                SKSE::log::warn(
                    "CFT EXTERIOR BOUNDS unresolved base connection={} event={} base={}:{:08X}",
                    key.sender,
                    key.eventID,
                    placement.pluginName,
                    placement.localFormID);
                continue;
            }

            auto mirror = anchor->PlaceObjectAtMe(base, false);
            if (!mirror) {
                SKSE::log::warn(
                    "CFT EXTERIOR BOUNDS PlaceObjectAtMe failed connection={} event={} anchor={:08X}",
                    key.sender,
                    key.eventID,
                    anchor->GetFormID());
                continue;
            }

            auto* actualParentCell = mirror->GetParentCell();
            if (actualParentCell != loadedCell) {
                SKSE::log::warn(
                    "CFT EXTERIOR BOUNDS rejected wrong parent connection={} event={} mirror={:08X} parentCell={:08X} expectedCell={:08X}",
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
                "CFT EXTERIOR BOUNDS PLACE created connection={} event={} mirror={:08X} parentCell={:08X} expectedCell={:08X} tent={} 3dLoaded={} pos=({:.2f},{:.2f},{:.2f})",
                key.sender,
                key.eventID,
                mirror->GetFormID(),
                actualParentCell->GetFormID(),
                loadedCell->GetFormID(),
                placement.isTent ? 1 : 0,
                mirror->Is3DLoaded() ? 1 : 0,
                placement.x,
                placement.y,
                placement.z);
        }
    }

    // Legacy 0.2.8 Papyrus materialization bridge. The exterior path no longer
    // emits these requests, but the native entry points remain for save/PEX compatibility.
    void CampfireSync::RequestRemoteMaterialization(
        const RemoteKey&,
        const RemotePlacement&,
        RE::TESObjectCELL*)
    {}

    bool CampfireSync::IsRemoteMaterializationRequestValid(std::uint32_t) const
    {
        return false;
    }

    float CampfireSync::GetRemoteMaterializationX(std::uint32_t) const
    {
        return 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationY(std::uint32_t) const
    {
        return 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationZ(std::uint32_t) const
    {
        return 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationAngleX(std::uint32_t) const
    {
        return 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationAngleY(std::uint32_t) const
    {
        return 0.0f;
    }

    float CampfireSync::GetRemoteMaterializationAngleZ(std::uint32_t) const
    {
        return 0.0f;
    }

    void CampfireSync::CompleteRemoteMaterialization(
        std::uint32_t requestID,
        RE::TESObjectREFR* reference)
    {
        if (reference) {
            DeleteMirror(reference->CreateRefHandle());
        }
        SKSE::log::debug(
            "CFT PAPYRUS MATERIALIZE legacy completion ignored request={}",
            requestID);
    }

    void CampfireSync::FailRemoteMaterialization(std::uint32_t requestID)
    {
        SKSE::log::debug(
            "CFT PAPYRUS MATERIALIZE legacy failure ignored request={}",
            requestID);
    }
}
