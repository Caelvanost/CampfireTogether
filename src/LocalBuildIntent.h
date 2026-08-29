#pragma once

namespace CampfireTogether::LocalBuildIntent
{
    void RegisterInputSink();
    void Reset();
    [[nodiscard]] bool Consume();
}
