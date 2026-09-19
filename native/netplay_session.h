// Fixed-delay lockstep input exchange between two native Melee builds over UDP.
//
// Both peers run the same binary from the same seed and save, so the game state is a pure function
// of the sequence of pad polls. Every PADRead is one poll: the local pad sampled at poll p is
// scheduled for poll p + delay and sent to the peer; poll p can only complete once the peer's pad
// for p has arrived, so both games apply identical inputs at identical polls. Each packet repeats
// the last kRedundancy inputs, so a lost datagram costs nothing unless several in a row vanish.
// A per-frame digest of the simulation travels in the same packets; a mismatch is a desync.
//
// Single-threaded by design: poll() and frame_hash() are called from the game thread only.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

struct sockaddr_in; // <netinet/in.h> stays out of this header

namespace netplay {

// Melee's 12-byte PADStatus; err 0 = connected, -1 = no controller.
struct Pad {
    uint16_t button = 0;
    int8_t stick_x = 0, stick_y = 0, substick_x = 0, substick_y = 0;
    uint8_t trigger_left = 0, trigger_right = 0, analog_a = 0, analog_b = 0;
    int8_t err = 0;
    uint8_t reserved = 0;
};
static_assert(sizeof(Pad) == 12);

constexpr uint32_t kMagic = 0x504E4C4Du; // "MLNP"
constexpr uint8_t kProtocolVersion = 1;
constexpr unsigned kRedundancy = 8;
constexpr unsigned kRing = 1024; // polls and frames remembered; must exceed delay + redundancy

// kKeepalive is sent by a helper thread four times a second whatever the game thread is doing, so a
// peer that spends half a minute in a loading screen (no pad polls, no input packets) is still known
// to be alive; the session is lost only after stall_timeout_ms without any packet at all.
enum PacketType : uint8_t { kReady = 1, kInput = 2, kBye = 3, kKeepalive = 4 };

#pragma pack(push, 1)
struct Packet {
    uint32_t magic;
    uint8_t version, type, player, count;
    uint8_t delay, reserved[3];
    uint32_t build;      // 32-bit digest of the running binary; both sides must match
    uint32_t last_index; // poll index of pads[count - 1]; pads[i] is for last_index - (count - 1 - i)
    uint32_t ack;        // highest poll index received from the peer so far
    uint32_t hash_frame; // logic frame the digest below belongs to (0 = none yet)
    uint32_t hash;
    uint32_t send_ms;    // sender's clock, echoed back for the round-trip estimate
    uint32_t echo_ms;    // the last send_ms received from the peer (0 = none)
    Pad pads[kRedundancy];
};
#pragma pack(pop)

struct Config {
    int local_player = 0;         // GameCube port this side plays on (0 = host, 1 = guest)
    unsigned delay = 3;           // polls of input delay
    std::string peer_ip;          // dotted quad
    uint16_t local_udp_port = 0;  // bind here
    uint16_t peer_udp_port = 0;   // send there
    uint32_t build = 0;
    unsigned ready_timeout_ms = 90000; // waiting for the peer to appear at start
    unsigned stall_timeout_ms = 10000; // waiting for one remote poll before giving up
    unsigned test_loss_percent = 0;    // tests only: drop this share of outgoing datagrams
    std::function<void(const std::string&)> log;
};

struct Stats {
    bool connected = false;
    bool lost = false;      // stall timeout: the session is over, the game keeps running locally
    bool desync = false;
    uint32_t desync_frame = 0;
    uint32_t desync_local = 0, desync_remote = 0;
    uint32_t polls = 0;
    unsigned stalls = 0;    // polls that had to wait for the remote input
    double stall_ms = 0;    // total time spent waiting
    double worst_stall_ms = 0;
    unsigned ping_ms = 0;   // last round-trip estimate
    unsigned sent = 0, received = 0, rejected = 0, keepalives = 0;
    uint32_t remote_ack = 0; // highest of our polls the peer acknowledged
    uint32_t last_remote_hash_frame = 0;
};

class Session {
public:
    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool open(const Config& config);
    void close();
    bool is_open() const { return socket_ >= 0; }

    // Blocks until a packet from the peer arrives or ready_timeout_ms passes. True when connected.
    bool wait_ready();
    // One pad poll: stores/sends the local pad, waits for the remote one, fills out[4]
    // (every port err = -1 except the two players). Returns false once the connection is lost;
    // out then carries the local pad only.
    bool poll(const Pad& local, Pad out[4]);
    // Simulation digest for a logic frame, from the game thread after the frame's update.
    void frame_hash(uint32_t frame, uint32_t hash);
    void send_bye();
    // Drains the socket; poll() does this itself, this is for idle screens.
    void pump() { receive(); }

    const Stats& stats() const { return stats_; }
    const Config& config() const { return config_; }
    int local_player() const { return config_.local_player; }
    int remote_player() const { return config_.local_player == 0 ? 1 : 0; }

private:
    struct Slot { uint32_t index = 0; bool valid = false; Pad pad; };
    struct HashSlot { uint32_t frame = 0; bool valid = false; uint32_t hash = 0; };

    void send_packet(uint8_t type);
    void send_keepalive(); // helper thread; touches only the socket and constant fields
    void receive();
    void handle(const Packet& packet, size_t length);
    void compare_hash(uint32_t frame, uint32_t local, uint32_t remote);
    bool has_remote(uint32_t index) const {
        const Slot& s = remote_[index % kRing];
        return s.valid && s.index == index;
    }
    uint32_t now_ms() const;
    void log(const std::string& text) { if (config_.log) config_.log(text); }

    Config config_;
    Stats stats_;
    int socket_ = -1;
    ::sockaddr_in* peer_ = nullptr;
    uint32_t next_poll_ = 0;
    uint32_t latest_local_ = 0;   // highest local poll index stored (scheduled)
    uint32_t remote_ack_ = 0;     // highest contiguous remote poll index received
    bool any_remote_ = false;
    uint32_t last_remote_send_ms_ = 0;
    uint32_t latest_hash_frame_ = 0, latest_hash_ = 0;
    uint32_t last_send_ms_ = 0;
    uint32_t last_receive_ms_ = 0; // any valid packet from the peer
    std::thread keepalive_thread_;
    std::atomic_bool keepalive_running_{false};
    Slot local_[kRing];
    Slot remote_[kRing];
    HashSlot my_hash_[kRing];
    HashSlot remote_hash_[kRing];
};

} // namespace netplay
