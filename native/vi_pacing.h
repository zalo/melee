// Frame pacing for VIWaitForRetrace (vi_runtime.cpp): how many retraces to deliver when the game
// thread arrives late, and how much of the debt to forgive.
//
// Melee runs one logic update per queued controller poll and then draws once, and a poll happens at
// every retrace. On a GameCube the retrace is a hardware interrupt, so a slow frame leaves several
// polls queued and the next loop iteration catches up: the game keeps real time and drops displayed
// frames. Here the retrace is delivered by VIWaitForRetrace itself, so a late game thread gets one
// retrace per missed 16.7 ms period, capped so a long stall (a shader compile, a disc load) cannot
// snowball into a burst of updates, and the rest of the debt is dropped: beyond the cap the game slows
// down, as it does today without catch-up.
#pragma once
#include <chrono>

namespace vi_pacing {

constexpr std::chrono::nanoseconds kPeriod{16666667};
// HSD's pad queue holds 5 polls; 3 per wait leaves room for the poll a free-XFB wait adds.
constexpr unsigned kDefaultMaxRetraces = 3;

struct Plan {
    unsigned retraces;        // retraces (pad polls) to deliver in this wait, at least 1
    unsigned dropped_periods; // missed periods forgiven: when nonzero the caller re-bases the clock to now
};

// lateness: now minus the time the next retrace was due (negative when early). max_retraces <= 1
// disables catch-up: one retrace per wait and every missed period forgiven.
inline Plan plan(std::chrono::nanoseconds lateness, unsigned max_retraces = kDefaultMaxRetraces)
{
    Plan p{1, 0};
    if (lateness < kPeriod) return p;
    const unsigned missed = static_cast<unsigned>(lateness / kPeriod);
    const unsigned extra = max_retraces > 1 ? (missed < max_retraces - 1 ? missed : max_retraces - 1) : 0;
    p.retraces = 1 + extra;
    p.dropped_periods = missed - extra;
    return p;
}

} // namespace vi_pacing
