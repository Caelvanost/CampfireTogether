#include "PCH.h"

#include "CampfireSync.h"
#include "CampfireTogether/Version.h"
#include "CellTracker.h"
#include "LocalBuildIntent.h"
#include "PapyrusBridge.h"
#include "Serialization.h"
#include "STRPMClient.h"

namespace logger = SKSE::log;

namespace
{
    void SetupLog()
    {
        auto path = logger::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Unable to resolve SKSE log directory");
        }

        *path /= "CampfireTogether.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
        log->set_level(spdlog::level::trace);
        log->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(std::move(log));
    }

    void InitializeRuntime(const char* reason)
    {
        logger::info("CFT runtime init reason={}", reason);
        CampfireTogether::LocalBuildIntent::RegisterInputSink();
        CampfireTogether::CellTracker::Register();
        CampfireTogether::STRPMClient::GetSingleton().Initialize();
    }

    void ExchangeState(const char* reason)
    {
        logger::info("CFT state exchange reason={}", reason);
        auto& client = CampfireTogether::STRPMClient::GetSingleton();
        if (!client.Initialize()) {
            return;
        }

        CampfireTogether::CampfireSync::GetSingleton().BroadcastSnapshot();
        client.RequestSnapshots();
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        switch (message->type) {
        case SKSE::MessagingInterface::kInputLoaded:
            CampfireTogether::LocalBuildIntent::RegisterInputSink();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            InitializeRuntime("data-loaded");
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            logger::info("CFT preparing for save load");
            CampfireTogether::CampfireSync::GetSingleton().ResetRemoteState();
            CampfireTogether::LocalBuildIntent::Reset();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            CampfireTogether::CampfireSync::GetSingleton().ResetRemoteState();
            CampfireTogether::LocalBuildIntent::Reset();
            InitializeRuntime("post-load-game");
            ExchangeState("post-load-game");
            break;
        case SKSE::MessagingInterface::kNewGame:
            CampfireTogether::CampfireSync::GetSingleton().Reset();
            CampfireTogether::LocalBuildIntent::Reset();
            InitializeRuntime("new-game");
            ExchangeState("new-game");
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SetupLog();

    logger::info("Campfire Together v{} loading", CampfireTogether::Version::STRING);

    auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(CampfireTogether::PapyrusBridge::Register)) {
        logger::critical("Failed to register CampfireTogether Papyrus native functions");
        return false;
    }

    if (!CampfireTogether::Serialization::Register()) {
        logger::critical("Failed to register CampfireTogether serialization callbacks");
        return false;
    }

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        logger::critical("Failed to register SKSE messaging listener");
        return false;
    }

    logger::info("Campfire Together initialized");
    return true;
}
