#include "PCH.h"
#include "PapyrusBridge.h"

#include "FireStateSync.h"
#include "LocalBuildIntent.h"
#include "SharedCampSync.h"
#include "STRPMClient.h"

namespace CampfireTogether::PapyrusBridge
{
    namespace
    {
        std::mutex g_seatAssignmentLogMutex;
        std::unordered_map<RE::FormID, RE::FormID> g_lastSeatAssignment;

        [[nodiscard]] std::uint32_t RequestID(std::int32_t requestID)
        {
            return requestID > 0 ? static_cast<std::uint32_t>(requestID) : 0;
        }

        void ReportPlaced(
            RE::StaticFunctionTag*,
            RE::TESObjectREFR* placedRef,
            float positionX,
            float positionY,
            float positionZ,
            float angleX,
            float angleY,
            float angleZ,
            bool isTent)
        {
            SharedCampSync::GetSingleton().OnLocalPlaced(
                placedRef,
                positionX,
                positionY,
                positionZ,
                angleX,
                angleY,
                angleZ,
                isTent);

            if (!isTent) {
                FireStateSync::GetSingleton().TrackCampfireReference(placedRef);
            }
        }

        void ReportRemoved(
            RE::StaticFunctionTag*,
            RE::TESForm* baseForm,
            float positionX,
            float positionY,
            float positionZ,
            float angleX,
            float angleY,
            float angleZ,
            bool isTent)
        {
            SharedCampSync::GetSingleton().OnLocalRemoved(
                baseForm,
                positionX,
                positionY,
                positionZ,
                angleX,
                angleY,
                angleZ,
                isTent);
        }

        bool IsRemoteCampObject(RE::StaticFunctionTag*, RE::TESObjectREFR* reference)
        {
            return SharedCampSync::GetSingleton().IsRemoteCampObject(reference);
        }

        bool IsRemoteMaterializationRequestValid(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return SharedCampSync::GetSingleton().IsRemoteMaterializationRequestValid(RequestID(requestID));
        }

        float GetRemoteMaterializationX(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return SharedCampSync::GetSingleton().GetRemoteMaterializationX(RequestID(requestID));
        }

        float GetRemoteMaterializationY(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return SharedCampSync::GetSingleton().GetRemoteMaterializationY(RequestID(requestID));
        }

        float GetRemoteMaterializationZ(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return SharedCampSync::GetSingleton().GetRemoteMaterializationZ(RequestID(requestID));
        }

        float GetRemoteMaterializationAngleX(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return SharedCampSync::GetSingleton().GetRemoteMaterializationAngleX(RequestID(requestID));
        }

        float GetRemoteMaterializationAngleY(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return SharedCampSync::GetSingleton().GetRemoteMaterializationAngleY(RequestID(requestID));
        }

        float GetRemoteMaterializationAngleZ(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return SharedCampSync::GetSingleton().GetRemoteMaterializationAngleZ(RequestID(requestID));
        }

        void ReportRemoteMaterialized(
            RE::StaticFunctionTag*,
            std::int32_t requestID,
            RE::TESObjectREFR* reference)
        {
            SharedCampSync::GetSingleton().CompleteRemoteMaterialization(RequestID(requestID), reference);
        }

        void ReportRemoteMaterializationFailed(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            SharedCampSync::GetSingleton().FailRemoteMaterialization(RequestID(requestID));
        }

        std::int32_t GetTrackedCampfireCount(RE::StaticFunctionTag*)
        {
            auto& sync = FireStateSync::GetSingleton();
            sync.RefreshTrackedCampfires();
            return sync.GetTrackedCount();
        }

        RE::TESObjectREFR* GetTrackedCampfire(RE::StaticFunctionTag*, std::int32_t index)
        {
            return FireStateSync::GetSingleton().GetTracked(index);
        }

        bool CampfireStateNeedsApply(RE::StaticFunctionTag*, RE::TESObjectREFR* reference)
        {
            return FireStateSync::GetSingleton().NeedsApply(reference);
        }

        std::int32_t GetDesiredCampfireStage(RE::StaticFunctionTag*, RE::TESObjectREFR* reference)
        {
            return FireStateSync::GetSingleton().GetDesiredStage(reference);
        }

        std::int32_t GetDesiredCampfireSize(RE::StaticFunctionTag*, RE::TESObjectREFR* reference)
        {
            return FireStateSync::GetSingleton().GetDesiredSize(reference);
        }

        float GetDesiredCampfireRemainingHours(RE::StaticFunctionTag*, RE::TESObjectREFR* reference)
        {
            return FireStateSync::GetSingleton().GetDesiredRemainingHours(reference);
        }

        RE::TESForm* GetDesiredCampfireFuelLit(RE::StaticFunctionTag*, RE::TESObjectREFR* reference)
        {
            return FireStateSync::GetSingleton().GetDesiredFuelLit(reference);
        }

        RE::TESForm* GetDesiredCampfireFuelUnlit(RE::StaticFunctionTag*, RE::TESObjectREFR* reference)
        {
            return FireStateSync::GetSingleton().GetDesiredFuelUnlit(reference);
        }

        RE::TESForm* GetDesiredCampfireLight(RE::StaticFunctionTag*, RE::TESObjectREFR* reference)
        {
            return FireStateSync::GetSingleton().GetDesiredLight(reference);
        }

        RE::TESObjectREFR* GetAssignedCampfireSeat(
            RE::StaticFunctionTag*,
            RE::TESObjectREFR* campfire,
            RE::TESObjectREFR* seat1,
            RE::TESObjectREFR* seat2,
            RE::TESObjectREFR* seat3,
            RE::TESObjectREFR* seat4)
        {
            if (!campfire || !seat2) {
                return seat2;
            }

            std::vector<RE::TESObjectREFR*> seats;
            seats.reserve(4);
            const auto appendSeat = [&seats](RE::TESObjectREFR* seat) {
                if (!seat || seat->IsMarkedForDeletion()) {
                    return;
                }
                const auto duplicate = std::find_if(
                    seats.begin(),
                    seats.end(),
                    [seat](const auto* existing) {
                        return existing == seat || existing->GetFormID() == seat->GetFormID();
                    });
                if (duplicate == seats.end()) {
                    seats.push_back(seat);
                }
            };

            appendSeat(seat1);
            appendSeat(seat2);
            appendSeat(seat3);
            appendSeat(seat4);

            if (seats.size() <= 1) {
                return seat2;
            }

            std::size_t participantCount = 0;
            const auto ordinal = STRPMClient::GetSingleton().GetLocalSeatOrdinal(
                seats.size(),
                participantCount);

            // Offline / solo play preserves Campfire's vanilla player seat.
            if (!ordinal || participantCount <= 1) {
                return seat2;
            }

            if (participantCount > seats.size()) {
                SKSE::log::warn(
                    "CFT SIT insufficient seats camp={:08X} participants={} seats={}",
                    campfire->GetFormID(),
                    participantCount,
                    seats.size());
                return nullptr;
            }

            const auto& firePos = campfire->data.location;
            std::sort(
                seats.begin(),
                seats.end(),
                [&firePos](const auto* lhs, const auto* rhs) {
                    const auto& leftPos = lhs->data.location;
                    const auto& rightPos = rhs->data.location;
                    const auto leftAngle = std::atan2(
                        static_cast<double>(leftPos.y - firePos.y),
                        static_cast<double>(leftPos.x - firePos.x));
                    const auto rightAngle = std::atan2(
                        static_cast<double>(rightPos.y - firePos.y),
                        static_cast<double>(rightPos.x - firePos.x));
                    if (leftAngle != rightAngle) {
                        return leftAngle < rightAngle;
                    }

                    const auto leftDX = leftPos.x - firePos.x;
                    const auto leftDY = leftPos.y - firePos.y;
                    const auto rightDX = rightPos.x - firePos.x;
                    const auto rightDY = rightPos.y - firePos.y;
                    const auto leftRadius = leftDX * leftDX + leftDY * leftDY;
                    const auto rightRadius = rightDX * rightDX + rightDY * rightDY;
                    return leftRadius < rightRadius;
                });

            // Spread fewer players around the whole ring instead of packing them
            // into adjacent seats. For four seats: 2 players -> slots 0 and 2;
            // 3 players -> 0, 1 and 3; 4 players -> 0, 1, 2 and 3.
            const auto numerator = (*ordinal) * seats.size();
            const auto seatIndex = static_cast<std::size_t>(std::floor(
                (static_cast<double>(numerator) / static_cast<double>(participantCount)) + 0.5));
            if (seatIndex >= seats.size()) {
                return nullptr;
            }

            auto* selected = seats[seatIndex];
            if (selected) {
                bool changed = false;
                {
                    std::scoped_lock lock(g_seatAssignmentLogMutex);
                    const auto [it, inserted] = g_lastSeatAssignment.insert_or_assign(
                        campfire->GetFormID(),
                        selected->GetFormID());
                    changed = inserted || it->second != selected->GetFormID();
                }

                // insert_or_assign updates before the comparison above, so also log
                // the first assignment and rely on Papyrus to log actual swaps.
                if (changed) {
                    SKSE::log::info(
                        "CFT SIT assignment camp={:08X} seat={:08X} ordinal={} participants={} seats={}",
                        campfire->GetFormID(),
                        selected->GetFormID(),
                        *ordinal,
                        participantCount,
                        seats.size());
                }
            }
            return selected;
        }

        void ReportCampfireState(
            RE::StaticFunctionTag*,
            RE::TESObjectREFR* reference,
            std::int32_t stage,
            std::int32_t size,
            float remainingHours,
            RE::TESForm* fuelLit,
            RE::TESForm* fuelUnlit,
            RE::TESForm* light)
        {
            auto& sync = FireStateSync::GetSingleton();
            if (!sync.CanReportObserved(reference)) {
                return;
            }

            sync.ReportObserved(
                reference,
                stage,
                size,
                remainingHours,
                fuelLit,
                fuelUnlit,
                light);
        }

        void AcknowledgeCampfireState(RE::StaticFunctionTag*, RE::TESObjectREFR* reference)
        {
            FireStateSync::GetSingleton().AcknowledgeApplied(reference);
        }

        void StateBridgeReady(RE::StaticFunctionTag*)
        {
            SKSE::log::info("CFT PAPYRUS fire-state observer READY poll=2s seatSlots=1");
        }

        bool ConsumeLocalCampfirePower(
            RE::StaticFunctionTag*,
            std::string powerTag,
            RE::Actor* caster)
        {
            return LocalBuildIntent::ConsumePower(powerTag, caster);
        }

        bool AuthorizeNestedCampfirePower(RE::StaticFunctionTag*, std::string powerTag)
        {
            return LocalBuildIntent::AuthorizeNestedPower(powerTag);
        }

        bool ConsumeLocalBuildIntent(RE::StaticFunctionTag*)
        {
            return LocalBuildIntent::Consume();
        }

        void ReportRemoteBuildSuppressed(RE::StaticFunctionTag*, RE::Actor* caster)
        {
            SKSE::log::info("CFT REMOTE BUILD CAMPFIRE suppressed caster={:08X}", caster ? caster->GetFormID() : 0);
        }

        void ReportRemoteBedrollAccess(
            RE::StaticFunctionTag*,
            RE::TESObjectREFR* bedroll,
            RE::TESObjectREFR* tent)
        {
            SKSE::log::info(
                "CFT REMOTE BEDROLL ACCESS bedroll={:08X} tent={:08X}",
                bedroll ? bedroll->GetFormID() : 0,
                tent ? tent->GetFormID() : 0);
        }

        void BridgeReady(RE::StaticFunctionTag*)
        {
            SKSE::log::info("CFT PAPYRUS Campfire event listener READY sharedRegistry=1");
        }
    }

    bool Register(RE::BSScript::IVirtualMachine* vm)
    {
        if (!vm) {
            return false;
        }

        vm->RegisterFunction("ReportPlaced", "CampfireTogetherNative", ReportPlaced);
        vm->RegisterFunction("ReportRemoved", "CampfireTogetherNative", ReportRemoved);
        vm->RegisterFunction("IsRemoteCampObject", "CampfireTogetherNative", IsRemoteCampObject);
        vm->RegisterFunction("IsRemoteMaterializationRequestValid", "CampfireTogetherNative", IsRemoteMaterializationRequestValid);
        vm->RegisterFunction("GetRemoteMaterializationX", "CampfireTogetherNative", GetRemoteMaterializationX);
        vm->RegisterFunction("GetRemoteMaterializationY", "CampfireTogetherNative", GetRemoteMaterializationY);
        vm->RegisterFunction("GetRemoteMaterializationZ", "CampfireTogetherNative", GetRemoteMaterializationZ);
        vm->RegisterFunction("GetRemoteMaterializationAngleX", "CampfireTogetherNative", GetRemoteMaterializationAngleX);
        vm->RegisterFunction("GetRemoteMaterializationAngleY", "CampfireTogetherNative", GetRemoteMaterializationAngleY);
        vm->RegisterFunction("GetRemoteMaterializationAngleZ", "CampfireTogetherNative", GetRemoteMaterializationAngleZ);
        vm->RegisterFunction("ReportRemoteMaterialized", "CampfireTogetherNative", ReportRemoteMaterialized);
        vm->RegisterFunction("ReportRemoteMaterializationFailed", "CampfireTogetherNative", ReportRemoteMaterializationFailed);

        vm->RegisterFunction("GetTrackedCampfireCount", "CampfireTogetherNative", GetTrackedCampfireCount);
        vm->RegisterFunction("GetTrackedCampfire", "CampfireTogetherNative", GetTrackedCampfire);
        vm->RegisterFunction("CampfireStateNeedsApply", "CampfireTogetherNative", CampfireStateNeedsApply);
        vm->RegisterFunction("GetDesiredCampfireStage", "CampfireTogetherNative", GetDesiredCampfireStage);
        vm->RegisterFunction("GetDesiredCampfireSize", "CampfireTogetherNative", GetDesiredCampfireSize);
        vm->RegisterFunction("GetDesiredCampfireRemainingHours", "CampfireTogetherNative", GetDesiredCampfireRemainingHours);
        vm->RegisterFunction("GetDesiredCampfireFuelLit", "CampfireTogetherNative", GetDesiredCampfireFuelLit);
        vm->RegisterFunction("GetDesiredCampfireFuelUnlit", "CampfireTogetherNative", GetDesiredCampfireFuelUnlit);
        vm->RegisterFunction("GetDesiredCampfireLight", "CampfireTogetherNative", GetDesiredCampfireLight);
        vm->RegisterFunction("GetAssignedCampfireSeat", "CampfireTogetherNative", GetAssignedCampfireSeat);
        vm->RegisterFunction("ReportCampfireState", "CampfireTogetherNative", ReportCampfireState);
        vm->RegisterFunction("AcknowledgeCampfireState", "CampfireTogetherNative", AcknowledgeCampfireState);
        vm->RegisterFunction("StateBridgeReady", "CampfireTogetherNative", StateBridgeReady);

        vm->RegisterFunction("ConsumeLocalCampfirePower", "CampfireTogetherNative", ConsumeLocalCampfirePower);
        vm->RegisterFunction("AuthorizeNestedCampfirePower", "CampfireTogetherNative", AuthorizeNestedCampfirePower);
        vm->RegisterFunction("ConsumeLocalBuildIntent", "CampfireTogetherNative", ConsumeLocalBuildIntent);
        vm->RegisterFunction("ReportRemoteBuildSuppressed", "CampfireTogetherNative", ReportRemoteBuildSuppressed);
        vm->RegisterFunction("ReportRemoteBedrollAccess", "CampfireTogetherNative", ReportRemoteBedrollAccess);
        vm->RegisterFunction("BridgeReady", "CampfireTogetherNative", BridgeReady);

        SKSE::log::info("CFT PAPYRUS native bridge READY class=CampfireTogetherNative sharedRegistry=1 fireState=1 powerGuard=1 seatSlots=1");
        return true;
    }
}
