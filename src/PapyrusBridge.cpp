#include "PCH.h"
#include "PapyrusBridge.h"

#include "CampfireSync.h"

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
        vm->RegisterFunction("BridgeReady", "CampfireTogetherNative", BridgeReady);

        SKSE::log::info("CFT PAPYRUS native bridge READY class=CampfireTogetherNative");
        return true;
    }
}
