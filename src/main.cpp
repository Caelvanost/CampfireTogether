#include "PCH.h"

#include "CampfireSync.h"
#include "CampfireTogether/Version.h"
#include "LocalBuildIntent.h"
#include "PapyrusBridge.h"
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

    void InitializeRuntime(const char* reason, bool reset)
    {
        logger::info("CFT runtime init reason={} reset={}", reason, reset ? 1 : 0);
        if (reset) {
            CampfireTogether::CampfireSync::GetSingleton().Reset();
            CampfireTogether::LocalBuildIntent::Reset();
        }
        CampfireTogether::LocalBuildIntent::RegisterInputSink();
        CampfireTogether::STRPMClient::GetSingleton().Initialize();
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
            InitializeRuntime("data-loaded", false);
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            InitializeRuntime("post-load-game", true);
            break;
        case SKSE::MessagingInterface::kNewGame:
            InitializeRuntime("new-game", true);
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

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        logger::critical("Failed to register SKSE messaging listener");
        return false;
    }

    logger::info("Campfire Together initialized");
    return true;
}
