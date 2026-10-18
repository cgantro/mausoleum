#pragma once

namespace A302RoomRuntimePolicy
{
    // Missing population information must never delete a live room. Negative
    // population represents an unavailable resolver result rather than emptiness.
    inline bool ShouldCollect(double LastTouched, double Now, double Timeout,
                              bool HasPopulationResolver, int Population)
    {
        const double SafeTimeout = Timeout < 10.0 ? 10.0 : Timeout;
        return HasPopulationResolver && Population == 0 && Now - LastTouched >= SafeTimeout;
    }
}
