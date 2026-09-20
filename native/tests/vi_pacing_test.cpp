// Catch-up pacing for VIWaitForRetrace: retraces delivered per wait against the game thread's lateness.
#include "../vi_pacing.h"
#include <cstdio>
#include <cstdlib>

using namespace std::chrono;
static int failures;
static void expect(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

int main()
{
    const auto P = vi_pacing::kPeriod;
    // On time or early: one retrace, nothing dropped.
    vi_pacing::Plan p = vi_pacing::plan(-P);
    expect(p.retraces == 1 && p.dropped_periods == 0, "early wait delivers one retrace");
    p = vi_pacing::plan(nanoseconds(0));
    expect(p.retraces == 1 && p.dropped_periods == 0, "on-time wait delivers one retrace");
    // Under a full period late: still one retrace, the phase carries over (fractions add up over frames).
    p = vi_pacing::plan(P - nanoseconds(1));
    expect(p.retraces == 1 && p.dropped_periods == 0, "sub-period lateness is carried, not caught up");
    // A 45 ms frame at the RG351P's pace: 28 ms late after the first period, one extra retrace.
    p = vi_pacing::plan(milliseconds(28));
    expect(p.retraces == 2 && p.dropped_periods == 0, "one missed period gives two retraces");
    // Two missed periods: the cap of three retraces exactly.
    p = vi_pacing::plan(2 * P + milliseconds(3));
    expect(p.retraces == 3 && p.dropped_periods == 0, "two missed periods give three retraces");
    // A 210 ms stall (shader compile): capped burst, the remaining debt forgiven.
    p = vi_pacing::plan(milliseconds(210));
    expect(p.retraces == 3 && p.dropped_periods == 12 - 2, "long stall is capped and the rest dropped");
    // Custom cap of 4.
    p = vi_pacing::plan(milliseconds(210), 4);
    expect(p.retraces == 4 && p.dropped_periods == 12 - 3, "cap of four");
    // Catch-up off (cap 1 or 0): one retrace, every missed period forgiven, as before the feature.
    p = vi_pacing::plan(milliseconds(210), 1);
    expect(p.retraces == 1 && p.dropped_periods == 12, "cap 1 disables catch-up");
    p = vi_pacing::plan(milliseconds(60), 0);
    expect(p.retraces == 1 && p.dropped_periods == 3, "cap 0 disables catch-up");
    if (failures) return EXIT_FAILURE;
    std::puts("vi_pacing: all cases pass");
    return EXIT_SUCCESS;
}
