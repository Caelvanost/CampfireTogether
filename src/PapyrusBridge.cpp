#include "PCH.h"
#include "PapyrusBridge.h"

#include "FireStateSync.h"
#include "LocalBuildIntent.h"
#include "SharedCampSync.h"

namespace CampfireTogether::PapyrusBridge
{
    namespace
    {
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
            SKSE::log::info("CFT PAPYRUS fire-state observer READY poll=2s");
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
        vm->RegisterFunction("ReportCampfireState", "CampfireTogetherNative", ReportCampfireState);
        vm->RegisterFunction("AcknowledgeCampfireState", "CampfireTogetherNative", AcknowledgeCampfireState);
        vm->RegisterFunction("StateBridgeReady", "CampfireTogetherNative", StateBridgeReady);

        vm->RegisterFunction("ConsumeLocalBuildIntent", "CampfireTogetherNative", ConsumeLocalBuildIntent);
        vm->RegisterFunction("ReportRemoteBuildSuppressed", "CampfireTogetherNative", ReportRemoteBuildSuppressed);
        vm->RegisterFunction("ReportRemoteBedrollAccess", "CampfireTogetherNative", ReportRemoteBedrollAccess);
        vm->RegisterFunction("BridgeReady", "CampfireTogetherNative", BridgeReady);

        SKSE::log::info("CFT PAPYRUS native bridge READY class=CampfireTogetherNative sharedRegistry=1 fireState=1");
        return true;
    }
}
