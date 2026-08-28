#include "PCH.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace STRPM
{
    namespace
    {
        HMODULE LoadPluginModule(const wchar_t* moduleName) noexcept
        {
            if (!moduleName) {
                return nullptr;
            }

            auto module = GetModuleHandleW(moduleName);
            if (!module) {
                module = LoadLibraryW(moduleName);
            }
            if (!module) {
                std::wstring pluginPath = L"Data\\SKSE\\Plugins\\";
                pluginPath += moduleName;
                module = LoadLibraryW(pluginPath.c_str());
            }
            return module;
        }

        template <class T>
        const T* Query(HMODULE module, const char* exportName, std::uint32_t version) noexcept
        {
            if (!module || !exportName) {
                return nullptr;
            }
            const auto raw = GetProcAddress(module, exportName);
            if (!raw) {
                return nullptr;
            }
            using QueryFn = Result(STRPM_CALL*)(std::uint32_t, const T**);
            const auto query = reinterpret_cast<QueryFn>(raw);
            const T* api = nullptr;
            return query(version, &api) == Result::kOk && api && api->version == version ? api : nullptr;
        }
    }

    const Interface* LoadFromModule(const wchar_t* moduleName) noexcept
    {
        return Query<Interface>(LoadPluginModule(moduleName), kQueryInterfaceExportName, kInterfaceVersion);
    }

    const DiagnosticsInterface* LoadDiagnosticsFromModule(const wchar_t* moduleName) noexcept
    {
        return Query<DiagnosticsInterface>(LoadPluginModule(moduleName), kQueryDiagnosticsExportName, kDiagnosticsVersion);
    }

    const TransportInterface* LoadTransportFromModule(const wchar_t* moduleName) noexcept
    {
        return Query<TransportInterface>(LoadPluginModule(moduleName), kQueryTransportExportName, kTransportInterfaceVersion);
    }

    const ProxyResolverInterface* LoadProxyResolverFromModule(const wchar_t* moduleName) noexcept
    {
        return Query<ProxyResolverInterface>(LoadPluginModule(moduleName), kQueryProxyResolverExportName, kProxyResolverVersion);
    }

    const char* ResultToString(Result result) noexcept
    {
        switch (result) {
        case Result::kOk: return "ok";
        case Result::kNotAvailable: return "not available";
        case Result::kUnsupportedVersion: return "unsupported version";
        case Result::kInvalidArgument: return "invalid argument";
        case Result::kNotConnected: return "not connected";
        case Result::kChannelAlreadyRegistered: return "channel already registered";
        case Result::kChannelNotRegistered: return "channel not registered";
        case Result::kPayloadTooLarge: return "payload too large";
        case Result::kRateLimited: return "rate limited";
        case Result::kTransportError: return "transport error";
        case Result::kTargetNotFound: return "target not found";
        default: return "unknown result";
        }
    }
}
