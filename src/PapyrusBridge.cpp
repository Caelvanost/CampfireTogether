#include "PCH.h"
#include "PapyrusBridge.h"

#include "CampfireSync.h"
#include "LocalBuildIntent.h"

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
            CampfireSync::GetSingleton().OnLocalPlaced(
                placedRef,
                positionX,
                positionY,
                positionZ,
                angleX,
                angleY,
                angleZ,
                isTent);
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
            CampfireSync::GetSingleton().OnLocalRemoved(
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
            return CampfireSync::GetSingleton().IsRemoteCampObject(reference);
        }

        bool IsRemoteMaterializationRequestValid(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return CampfireSync::GetSingleton().IsRemoteMaterializationRequestValid(RequestID(requestID));
        }

        float GetRemoteMaterializationX(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return CampfireSync::GetSingleton().GetRemoteMaterializationX(RequestID(requestID));
        }

        float GetRemoteMaterializationY(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return CampfireSync::GetSingleton().GetRemoteMaterializationY(RequestID(requestID));
        }

        float GetRemoteMaterializationZ(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return CampfireSync::GetSingleton().GetRemoteMaterializationZ(RequestID(requestID));
        }

        float GetRemoteMaterializationAngleX(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return CampfireSync::GetSingleton().GetRemoteMaterializationAngleX(RequestID(requestID));
        }

        float GetRemoteMaterializationAngleY(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return CampfireSync::GetSingleton().GetRemoteMaterializationAngleY(RequestID(requestID));
        }

        float GetRemoteMaterializationAngleZ(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            return CampfireSync::GetSingleton().GetRemoteMaterializationAngleZ(RequestID(requestID));
        }

        void ReportRemoteMaterialized(
            RE::StaticFunctionTag*,
            std::int32_t requestID,
            RE::TESObjectREFR* reference)
        {
            CampfireSync::GetSingleton().CompleteRemoteMaterialization(RequestID(requestID), reference);
        }

        void ReportRemoteMaterializationFailed(RE::StaticFunctionTag*, std::int32_t requestID)
        {
            CampfireSync::GetSingleton().FailRemoteMaterialization(RequestID(requestID));
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
            SKSE::log::info("CFT PAPYRUS Campfire event listener READY remoteMaterialization=1");
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
        vm->RegisterFunction("ConsumeLocalBuildIntent", "CampfireTogetherNative", ConsumeLocalBuildIntent);
        vm->RegisterFunction("ReportRemoteBuildSuppressed", "CampfireTogetherNative", ReportRemoteBuildSuppressed);
        vm->RegisterFunction("ReportRemoteBedrollAccess", "CampfireTogetherNative", ReportRemoteBedrollAccess);
        vm->RegisterFunction("BridgeReady", "CampfireTogetherNative", BridgeReady);

        SKSE::log::info("CFT PAPYRUS native bridge READY class=CampfireTogetherNative remoteMaterialization=1");
        return true;
    }
}
