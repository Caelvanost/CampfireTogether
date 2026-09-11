#pragma once

namespace CampfireTogether::LocalBuildIntent
{
    void RegisterInputSink();
    void Reset();

    // Backward-compatible Build Campfire helper.
    [[nodiscard]] bool Consume();

    [[nodiscard]] bool ConsumePower(std::string_view powerTag, RE::Actor* caster);
    [[nodiscard]] bool AuthorizeNestedPower(std::string_view powerTag);
}
