#include "PCH.h"
#include "PapyrusBridge.h"

#include "CampfireSync.h"
#include "LocalBuildIntent.h"

namespace CampfireTogether::PapyrusBridge
{
    namespace
    {
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
            SKSE::log::info("CFT PAPYRUS Campfire event listener READY");
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
        vm->RegisterFunction("ConsumeLocalBuildIntent", "CampfireTogetherNative", ConsumeLocalBuildIntent);
        vm->RegisterFunction("ReportRemoteBuildSuppressed", "CampfireTogetherNative", ReportRemoteBuildSuppressed);
        vm->RegisterFunction("ReportRemoteBedrollAccess", "CampfireTogetherNative", ReportRemoteBedrollAccess);
        vm->RegisterFunction("BridgeReady", "CampfireTogetherNative", BridgeReady);

        SKSE::log::info("CFT PAPYRUS native bridge READY class=CampfireTogetherNative");
        return true;
    }
}
