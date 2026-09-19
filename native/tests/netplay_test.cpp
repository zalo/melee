// Two lockstep sessions on the loopback interface: inputs arrive at the scheduled poll on both
// sides, dropped datagrams are covered by the redundancy, hashes detect a desync, and a silent
// peer ends the session instead of hanging the game.
#include "../netplay_session.h"

#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#define CHECK(cond)                                                                                        \
    do {                                                                                                   \
        if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); std::exit(1); } \
    } while (0)

using netplay::Pad;
using netplay::Session;

static netplay::Config config(int player, uint16_t local, uint16_t peer) {
    netplay::Config c;
    c.local_player = player;
    c.delay = 3;
    c.peer_ip = "127.0.0.1";
    c.local_udp_port = local;
    c.peer_udp_port = peer;
    c.build = 0xC0FFEE;
    c.ready_timeout_ms = 5000;
    c.stall_timeout_ms = 800;
    c.log = [](const std::string& text) { std::fprintf(stderr, "  %s\n", text.c_str()); };
    return c;
}

static Pad pad_for(int player, uint32_t poll) {
    Pad p;
    p.button = static_cast<uint16_t>(0x100 * (player + 1) + (poll & 0xFF));
    p.stick_x = static_cast<int8_t>(poll % 100);
    p.err = 0;
    return p;
}

// Each side runs its polls on its own thread, as two games would; returns the pads it saw.
struct Run {
    std::vector<std::array<Pad, 4>> seen;
    netplay::Stats stats;
    bool ok = true;
};

static Run play(int player, uint16_t local, uint16_t peer, unsigned polls, unsigned hash_every_frame_from, bool diverge_hash) {
    Run run;
    Session session;
    CHECK(session.open(config(player, local, peer)));
    for (unsigned p = 0; p < polls; ++p) {
        std::array<Pad, 4> out{};
        const bool alive = session.poll(pad_for(player, p), out.data());
        run.seen.push_back(out);
        // One logic frame per poll here; the digest differs on one side from a chosen frame on.
        const uint32_t frame = p + 1;
        uint32_t hash = 0x1000 + frame;
        if (diverge_hash && frame >= hash_every_frame_from) hash ^= 0xBAD;
        session.frame_hash(frame, hash);
        if (!alive) { run.ok = false; break; }
    }
    run.stats = session.stats();
    // No BYE here: the other side may still be completing its last polls (a BYE ends its session,
    // which is right in the game and wrong for this comparison). The destructor closes the socket.
    return run;
}

int main() {
    // 1. Plain exchange: every poll sees the local pad from delay polls ago and the remote one for
    //    the same poll; the first `delay` polls are neutral on both.
    {
        Run a, b;
        std::thread ta([&] { a = play(0, 40001, 40002, 120, 0, false); });
        std::thread tb([&] { b = play(1, 40002, 40001, 120, 0, false); });
        ta.join(); tb.join();
        CHECK(a.ok && b.ok);
        CHECK(a.seen.size() == 120 && b.seen.size() == 120);
        for (unsigned p = 0; p < 120; ++p) {
            const auto& pa = a.seen[p];
            const auto& pb = b.seen[p];
            // Both sides saw identical four-port input at poll p.
            for (int port = 0; port < 4; ++port) CHECK(std::memcmp(&pa[port], &pb[port], sizeof(Pad)) == 0);
            CHECK(pa[2].err == -1 && pa[3].err == -1);
            if (p < 3) {
                CHECK(pa[0].button == 0 && pa[1].button == 0 && pa[0].err == 0 && pa[1].err == 0);
            } else {
                CHECK(pa[0].button == pad_for(0, p - 3).button);
                CHECK(pa[1].button == pad_for(1, p - 3).button);
                CHECK(pa[1].stick_x == pad_for(1, p - 3).stick_x);
            }
        }
        CHECK(!a.stats.desync && !b.stats.desync);
        CHECK(a.stats.polls == 120 && b.stats.polls == 120);
        std::fprintf(stderr, "exchange ok: a stalls %u (%.1f ms) b stalls %u (%.1f ms) ping %u/%u ms\n", a.stats.stalls,
                     a.stats.stall_ms, b.stats.stalls, b.stats.stall_ms, a.stats.ping_ms, b.stats.ping_ms);
    }
    // 2. Desync detection: side B's digest diverges from frame 50; both sides report frame 50.
    {
        Run a, b;
        std::thread ta([&] { a = play(0, 40003, 40004, 90, 50, false); });
        std::thread tb([&] { b = play(1, 40004, 40003, 90, 50, true); });
        ta.join(); tb.join();
        CHECK(a.ok && b.ok);
        CHECK(a.stats.desync && b.stats.desync);
        CHECK(a.stats.desync_frame == 50 && b.stats.desync_frame == 50);
        CHECK(a.stats.desync_local == 0x1000 + 50 && a.stats.desync_remote == ((0x1000 + 50) ^ 0xBAD));
        std::fprintf(stderr, "desync detection ok at frame %u\n", a.stats.desync_frame);
    }
    // 3. A peer that never shows up: wait_ready gives up and the game continues offline with the
    //    local pad on the local port.
    {
        Session lonely;
        auto c = config(0, 40005, 40006);
        c.ready_timeout_ms = 300;
        CHECK(lonely.open(c));
        Pad out[4];
        CHECK(!lonely.poll(pad_for(0, 0), out));
        CHECK(lonely.stats().lost && !lonely.stats().connected);
        CHECK(out[0].button == pad_for(0, 0).button && out[1].err == -1);
        std::fprintf(stderr, "absent peer ok\n");
    }
    // 4. A peer that vanishes mid-session: the stall times out, the session reports loss and the
    //    remaining polls return the local pad instead of hanging.
    {
        Run a;
        Session b;
        std::thread ta([&] { a = play(0, 40007, 40008, 200, 0, false); });
        CHECK(b.open(config(1, 40008, 40007)));
        for (unsigned p = 0; p < 30; ++p) { Pad out[4]; b.poll(pad_for(1, p), out); }
        b.close(); // silently gone
        ta.join();
        CHECK(!a.ok);
        CHECK(a.stats.lost);
        CHECK(a.seen.size() > 30 && a.seen.size() < 200);
        std::fprintf(stderr, "lost peer ok after %zu polls (worst stall %.0f ms)\n", a.seen.size(), a.stats.worst_stall_ms);
    }
    // 5. A peer that stops polling for longer than the stall timeout but stays alive (a loading
    //    screen): its keepalives keep the session open, and the poll completes when it returns.
    {
        Run a;
        Session b;
        CHECK(b.open(config(1, 40009, 40010)));
        std::thread ta([&] { a = play(0, 40010, 40009, 40, 0, false); });
        for (unsigned p = 0; p < 20; ++p) { Pad out[4]; b.poll(pad_for(1, p), out); }
        std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // 2.5x the stall timeout, no polls
        for (unsigned p = 20; p < 40; ++p) { Pad out[4]; b.poll(pad_for(1, p), out); }
        ta.join();
        CHECK(a.ok);
        CHECK(!a.stats.lost && a.seen.size() == 40);
        CHECK(a.stats.worst_stall_ms > 1500);
        CHECK(a.stats.keepalives >= 4);
        std::fprintf(stderr, "loading peer ok: worst stall %.0f ms, %u keepalives\n", a.stats.worst_stall_ms, a.stats.keepalives);
    }
    std::fprintf(stderr, "netplay_test: all ok\n");
    return 0;
}
