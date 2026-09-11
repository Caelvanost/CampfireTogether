#include "PCH.h"
#include "SharedCampSync.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr float kStateMatchRadius = 64.0f;

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
    }

    bool SharedCampSync::TryGetCampKey(
        RE::TESObjectREFR* reference,
        std::uint64_t& originNodeID,
        std::uint64_t& objectID,
        bool& isTent) const
    {
        originNodeID = 0;
        objectID = 0;
        isTent = false;
        if (!reference || reference->IsMarkedForDeletion()) {
            return false;
        }

        const auto referenceFormID = reference->GetFormID();
        auto* base = reference->GetBaseObject();
        if (!base || base->GetFormID() == 0) {
            return false;
        }

        auto* ownerFile = base->GetFile(0);
        if (!ownerFile || !ownerFile->fileName[0]) {
            return false;
        }

        const auto baseLocalFormID = base->GetLocalFormID();
        if (baseLocalFormID == 0) {
            return false;
        }

        const auto& position = reference->data.location;
        const float maxDistance = kStateMatchRadius * kStateMatchRadius;

        std::scoped_lock lock(_mutex);

        for (const auto& [id, mirror] : _mirrors) {
            auto tracked = mirror.handle.get();
            if (tracked && tracked->GetFormID() == referenceFormID) {
                const auto recordIt = _sharedCamps.find(id);
                if (recordIt == _sharedCamps.end() || recordIt->second.deleted) {
                    return false;
                }
                originNodeID = id.originNodeID;
                objectID = id.objectID;
                isTent = recordIt->second.isTent;
                return true;
            }
        }

        for (const auto& [id, record] : _sharedCamps) {
            if (record.deleted ||
                record.localFormID != baseLocalFormID ||
                record.pluginName != ownerFile->fileName) {
                continue;
            }

            if (DistanceSquared(
                    record.x,
                    record.y,
                    record.z,
                    position.x,
                    position.y,
                    position.z) > maxDistance) {
                continue;
            }

            originNodeID = id.originNodeID;
            objectID = id.objectID;
            isTent = record.isTent;
            return true;
        }

        return false;
    }

    bool SharedCampSync::IsCampActive(std::uint64_t originNodeID, std::uint64_t objectID) const
    {
        if (originNodeID == 0 || objectID == 0) {
            return false;
        }

        std::scoped_lock lock(_mutex);
        const auto it = _sharedCamps.find(CampID{ originNodeID, objectID });
        return it != _sharedCamps.end() && !it->second.deleted;
    }

    RE::TESObjectREFR* SharedCampSync::FindPhysicalCamp(
        std::uint64_t originNodeID,
        std::uint64_t objectID) const
    {
        if (originNodeID == 0 || objectID == 0) {
            return nullptr;
        }

        std::scoped_lock lock(_mutex);
        const CampID id{ originNodeID, objectID };
        const auto recordIt = _sharedCamps.find(id);
        if (recordIt == _sharedCamps.end() || recordIt->second.deleted) {
            return nullptr;
        }

        const auto mirrorIt = _mirrors.find(id);
        if (mirrorIt == _mirrors.end()) {
            return nullptr;
        }

        auto reference = mirrorIt->second.handle.get();
        return reference && !reference->IsMarkedForDeletion() ? reference.get() : nullptr;
    }

    std::uint64_t SharedCampSync::GetLocalNodeID()
    {
        return EnsureLocalNodeID();
    }
}
