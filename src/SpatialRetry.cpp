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

        RE::TESObjectREFR* anchor = nullptr;
        if (auto* player = RE::PlayerCharacter::GetSingleton();
            player && player->GetParentCell() == loadedCell) {
            anchor = player;
        }

        if (!anchor) {
            loadedCell->ForEachReference([&anchor](RE::TESObjectREFR& reference) {
                if (!reference.IsMarkedForDeletion()) {
                    anchor = std::addressof(reference);
                    return RE::BSContainer::ForEachResult::kStop;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        if (!anchor) {
            SKSE::log::debug(
                "CFT EXTERIOR GRID pending no anchor grid=({},{}) cell={:08X}",
                loadedCoordinates->cellX,
                loadedCoordinates->cellY,
                loadedCell->GetFormID());
            return;
        }

        std::vector<std::pair<RemoteKey, RemotePlacement>> candidates;
        {
            std::scoped_lock lock(_mutex);
            candidates.reserve(_remotePlacements.size());
            for (const auto& entry : _remotePlacements) {
                const auto& placement = entry.second;
                if (WorldToCell(placement.x) != loadedCoordinates->cellX ||
                    WorldToCell(placement.y) != loadedCoordinates->cellY) {
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

        SKSE::log::info(
            "CFT EXTERIOR GRID loaded cell={:08X} grid=({},{}) candidates={}",
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
                const auto stateIt = _remotePlacements.find(key);
                if (stateIt == _remotePlacements.end()) {
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

            auto mirror = anchor->PlaceObjectAtMe(base, false);
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
                "CFT EXTERIOR GRID PLACE created connection={} event={} mirror={:08X} grid=({},{}) tent={} pos=({:.2f},{:.2f},{:.2f})",
                key.sender,
                key.eventID,
                mirror->GetFormID(),
                loadedCoordinates->cellX,
                loadedCoordinates->cellY,
                placement.isTent ? 1 : 0,
                placement.x,
                placement.y,
                placement.z);
        }
    }
}
