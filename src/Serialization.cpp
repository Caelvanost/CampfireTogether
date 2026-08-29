#include "PCH.h"
#include "Serialization.h"

#include "CampfireSync.h"

namespace CampfireTogether::Serialization
{
    namespace
    {
        constexpr std::uint32_t kSerializationID = 0x32465443;  // "CFT2"

        void OnSave(SKSE::SerializationInterface* serialization)
        {
            CampfireSync::GetSingleton().SavePersistentState(serialization);
        }

        void OnLoad(SKSE::SerializationInterface* serialization)
        {
            CampfireSync::GetSingleton().LoadPersistentState(serialization);
        }

        void OnRevert(SKSE::SerializationInterface*)
        {
            CampfireSync::GetSingleton().ClearPersistentState();
        }
    }

    bool Register()
    {
        auto* serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            SKSE::log::critical("CFT SERIALIZATION unavailable");
            return false;
        }

        serialization->SetUniqueID(kSerializationID);
        serialization->SetSaveCallback(OnSave);
        serialization->SetLoadCallback(OnLoad);
        serialization->SetRevertCallback(OnRevert);

        SKSE::log::info("CFT SERIALIZATION READY id=CFT2");
        return true;
    }
}
