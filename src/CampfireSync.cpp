#include "PCH.h"
#include "CampfireSync.h"

#include "STRPMClient.h"

namespace CampfireTogether
{
    namespace
    {
        constexpr std::size_t kMaxLocalHistory = 256;
        constexpr float kRemovalMatchRadius = 64.0f;

        Protocol::Packet MakePacket(
            Protocol::PacketType type,
            std::uint64_t eventID,
            RE::FormID baseFormID,
            float px,
            float py,
            float pz,
            float ax,
            float ay,
            float az,
            bool isTent)
        {
            Protocol::Packet packet{};
            packet.type = type;
            packet.eventID = eventID;
            packet.baseFormID = baseFormID;
            packet.flags = isTent ? Protocol::kTent : Protocol::kNone;
            packet.positionX = px;
            packet.positionY = py;
            packet.positionZ = pz;
            packet.angleX = ax;
            packet.angleY = ay;
            packet.angleZ = az;
            return packet;
        }

        float DistanceSquared(float ax, float ay, float az, float bx, float by, float bz)
        {
            const float dx = ax - bx;
            const float dy = ay - by;
            const float dz = az - bz;
            return dx * dx + dy * dy + dz * dz;
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

        const auto eventID = _nextEventID.fetch_add(1);
        const auto baseFormID = base->GetFormID();

        {
            std::scoped_lock lock(_mutex);
            _localPlacements.push_back({ eventID, baseFormID, positionX, positionY, positionZ, isTent });
            while (_localPlacements.size() > kMaxLocalHistory) {
                _localPlacements.pop_front();
            }
        }

        SKSE::log::info("CFT LOCAL PLACE ref={:08X} base={:08X} event={} tent={} pos=({:.2f},{:.2f},{:.2f}) rot=({:.2f},{:.2f},{:.2f})", placedRef->GetFormID(), baseFormID, eventID, isTent ? 1 : 0, positionX, positionY, positionZ, angleX, angleY, angleZ);

        STRPMClient::GetSingleton().Send(MakePacket(
            Protocol::PacketType::kPlace,
            eventID,
            baseFormID,
            positionX,
            positionY,
            positionZ,
            angleX,
            angleY,
            angleZ,
            isTent));
    }

    std::uint64_t CampfireSync::MatchLocalRemoval(RE::FormID baseFormID, float x, float y, float z, bool isTent)
    {
        std::scoped_lock lock(_mutex);

        auto best = _localPlacements.end();
        float bestDistance = kRemovalMatchRadius * kRemovalMatchRadius;
        for (auto it = _localPlacements.begin(); it != _localPlacements.end(); ++it) {
            if (it->baseFormID != baseFormID || it->isTent != isTent) {
                continue;
            }
            const auto distance = DistanceSquared(it->x, it->y, it->z, x, y, z);
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = it;
            }
        }

        if (best == _localPlacements.end()) {
            return 0;
        }

        const auto eventID = best->eventID;
        _localPlacements.erase(best);
        return eventID;
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

        const auto baseFormID = baseForm->GetFormID();
        const auto eventID = MatchLocalRemoval(baseFormID, positionX, positionY, positionZ, isTent);

        SKSE::log::info("CFT LOCAL REMOVE base={:08X} event={} tent={} pos=({:.2f},{:.2f},{:.2f})", baseFormID, eventID, isTent ? 1 : 0, positionX, positionY, positionZ);

        STRPMClient::GetSingleton().Send(MakePacket(
            Protocol::PacketType::kRemove,
            eventID,
            baseFormID,
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
            SpawnRemote(sender, packet);
            break;
        case Protocol::PacketType::kRemove:
            RemoveRemote(sender, packet);
            break;
        default:
            break;
        }
    }

    void CampfireSync::SpawnRemote(STRPM::ConnectionID sender, const Protocol::Packet& packet)
    {
        const RemoteKey key{ sender, packet.eventID };
        {
            std::scoped_lock lock(_mutex);
            if (packet.eventID != 0 && _remoteMirrors.contains(key)) {
                SKSE::log::trace("CFT REMOTE PLACE duplicate ignored connection={} event={}", sender, packet.eventID);
                return;
            }
        }

        const auto proxyID = STRPMClient::GetSingleton().ResolveProxy(sender);
        if (!proxyID) {
            SKSE::log::warn("CFT REMOTE PLACE deferred/ignored: no STR proxy connection={} event={}", sender, packet.eventID);
            return;
        }

        auto* anchor = RE::TESForm::LookupByID<RE::TESObjectREFR>(*proxyID);
        auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(packet.baseFormID);
        if (!anchor || !base) {
            SKSE::log::warn("CFT REMOTE PLACE failed connection={} event={} proxy={:08X} base={:08X} anchor={} baseFound={}", sender, packet.eventID, *proxyID, packet.baseFormID, anchor ? 1 : 0, base ? 1 : 0);
            return;
        }

        auto mirror = anchor->PlaceObjectAtMe(base, false);
        if (!mirror) {
            SKSE::log::warn("CFT REMOTE PLACE PlaceObjectAtMe failed connection={} event={} base={:08X}", sender, packet.eventID, packet.baseFormID);
            return;
        }

        mirror->SetPosition(packet.positionX, packet.positionY, packet.positionZ);
        mirror->data.angle = {
            RE::deg_to_rad(packet.angleX),
            RE::deg_to_rad(packet.angleY),
            RE::deg_to_rad(packet.angleZ)
        };
        mirror->Update3DPosition(true);

        const auto handle = mirror->CreateRefHandle();
        if (packet.eventID != 0) {
            std::scoped_lock lock(_mutex);
            _remoteMirrors.emplace(key, handle);
        }

        SKSE::log::info("CFT REMOTE PLACE created connection={} event={} mirror={:08X} base={:08X} tent={} pos=({:.2f},{:.2f},{:.2f})", sender, packet.eventID, mirror->GetFormID(), packet.baseFormID, (packet.flags & Protocol::kTent) ? 1 : 0, packet.positionX, packet.positionY, packet.positionZ);
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

    void CampfireSync::RemoveRemote(STRPM::ConnectionID sender, const Protocol::Packet& packet)
    {
        RE::ObjectRefHandle handle{};
        bool found = false;

        {
            std::scoped_lock lock(_mutex);
            if (packet.eventID != 0) {
                const RemoteKey key{ sender, packet.eventID };
                if (const auto it = _remoteMirrors.find(key); it != _remoteMirrors.end()) {
                    handle = it->second;
                    _remoteMirrors.erase(it);
                    found = true;
                }
            }

            if (!found) {
                for (auto it = _remoteMirrors.begin(); it != _remoteMirrors.end(); ++it) {
                    if (it->first.sender != sender) {
                        continue;
                    }
                    auto ref = it->second.get();
                    if (!ref || !ref->GetBaseObject() || ref->GetBaseObject()->GetFormID() != packet.baseFormID) {
                        continue;
                    }
                    const auto& p = ref->data.location;
                    if (DistanceSquared(p.x, p.y, p.z, packet.positionX, packet.positionY, packet.positionZ) <= kRemovalMatchRadius * kRemovalMatchRadius) {
                        handle = it->second;
                        _remoteMirrors.erase(it);
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found) {
            SKSE::log::warn("CFT REMOTE REMOVE no mirror match connection={} event={} base={:08X}", sender, packet.eventID, packet.baseFormID);
            return;
        }

        DeleteMirror(handle);
        SKSE::log::info("CFT REMOTE REMOVE deleted connection={} event={} base={:08X}", sender, packet.eventID, packet.baseFormID);
    }

    void CampfireSync::Reset()
    {
        std::deque<RE::ObjectRefHandle> mirrors;
        {
            std::scoped_lock lock(_mutex);
            for (const auto& [key, handle] : _remoteMirrors) {
                mirrors.push_back(handle);
            }
            _remoteMirrors.clear();
            _localPlacements.clear();
        }

        for (const auto& handle : mirrors) {
            DeleteMirror(handle);
        }
        SKSE::log::info("CFT state reset mirrors={}", mirrors.size());
    }
}
