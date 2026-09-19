#include "netplay_session.h"

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace netplay {
namespace {
using Clock = std::chrono::steady_clock;
const auto kEpoch = Clock::now();
} // namespace

Session::~Session() { close(); }

uint32_t Session::now_ms() const {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - kEpoch).count()) + 1;
}

bool Session::open(const Config& config) {
    close();
    config_ = config;
    stats_ = {};
    next_poll_ = 0;
    latest_local_ = 0;
    remote_ack_ = 0;
    any_remote_ = false;
    last_remote_send_ms_ = 0;
    latest_hash_frame_ = latest_hash_ = 0;
    for (auto& s : local_) s = {};
    for (auto& s : remote_) s = {};
    for (auto& h : my_hash_) h = {};
    for (auto& h : remote_hash_) h = {};
    if (config_.delay + kRedundancy + 8 >= kRing) {
        log("netplay: input delay too large");
        return false;
    }
    // Polls 0..delay-1 have no sampled input behind them: both sides apply (and send) neutral pads,
    // so the peer's first polls can complete without waiting for inputs that will never exist.
    for (uint32_t i = 0; i < config_.delay; ++i) local_[i % kRing] = {i, true, Pad{}};
    if (config_.delay > 0) latest_local_ = config_.delay - 1;
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        log(std::string("netplay: socket: ") + strerror(errno));
        return false;
    }
    int one = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(config_.local_udp_port);
    if (bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof local) < 0) {
        log(std::string("netplay: bind ") + std::to_string(config_.local_udp_port) + ": " + strerror(errno));
        close();
        return false;
    }
    fcntl(socket_, F_SETFL, fcntl(socket_, F_GETFL) | O_NONBLOCK);
    peer_ = new sockaddr_in{};
    peer_->sin_family = AF_INET;
    peer_->sin_port = htons(config_.peer_udp_port);
    if (inet_pton(AF_INET, config_.peer_ip.c_str(), &peer_->sin_addr) != 1) {
        log("netplay: bad peer address " + config_.peer_ip);
        close();
        return false;
    }
    log("netplay: player " + std::to_string(config_.local_player + 1) + ", delay " + std::to_string(config_.delay) +
        ", peer " + config_.peer_ip + ":" + std::to_string(config_.peer_udp_port) + ", local port " +
        std::to_string(config_.local_udp_port));
    last_receive_ms_ = 0;
    keepalive_running_ = true;
    keepalive_thread_ = std::thread([this] {
        while (keepalive_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (keepalive_running_) send_keepalive();
        }
    });
    return true;
}

void Session::close() {
    keepalive_running_ = false;
    if (keepalive_thread_.joinable()) keepalive_thread_.join();
    if (socket_ >= 0) ::close(socket_);
    socket_ = -1;
    delete peer_;
    peer_ = nullptr;
}

void Session::send_keepalive() {
    if (socket_ < 0 || !peer_) return;
    Packet packet{};
    packet.magic = kMagic;
    packet.version = kProtocolVersion;
    packet.type = kKeepalive;
    packet.player = static_cast<uint8_t>(config_.local_player);
    packet.delay = static_cast<uint8_t>(config_.delay);
    packet.build = config_.build;
    packet.send_ms = now_ms();
    sendto(socket_, &packet, offsetof(Packet, pads), 0, reinterpret_cast<sockaddr*>(peer_), sizeof *peer_);
}

void Session::send_packet(uint8_t type) {
    if (socket_ < 0) return;
    Packet packet{};
    packet.magic = kMagic;
    packet.version = kProtocolVersion;
    packet.type = type;
    packet.player = static_cast<uint8_t>(config_.local_player);
    packet.delay = static_cast<uint8_t>(config_.delay);
    packet.build = config_.build;
    packet.ack = any_remote_ ? remote_ack_ : 0;
    packet.hash_frame = latest_hash_frame_;
    packet.hash = latest_hash_;
    packet.send_ms = now_ms();
    packet.echo_ms = last_remote_send_ms_;
    unsigned count = 0;
    if (type == kInput && (latest_local_ != 0 || local_[0].valid)) {
        // The last kRedundancy scheduled polls, oldest first, ending at latest_local_.
        uint32_t first = latest_local_ + 1 >= kRedundancy ? latest_local_ + 1 - kRedundancy : 0;
        for (uint32_t i = first; i <= latest_local_ && count < kRedundancy; ++i) {
            const Slot& s = local_[i % kRing];
            if (!s.valid || s.index != i) continue;
            packet.pads[count++] = s.pad;
        }
        packet.last_index = latest_local_;
    }
    packet.count = static_cast<uint8_t>(count);
    const size_t length = offsetof(Packet, pads) + count * sizeof(Pad);
    if (config_.test_loss_percent) {
        // Deterministic pseudo-random drop so a test run is repeatable; keepalives are not dropped.
        static uint32_t x = 0x9E3779B9u;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        if (x % 100 < config_.test_loss_percent) { ++stats_.sent; return; }
    }
    if (sendto(socket_, &packet, length, 0, reinterpret_cast<sockaddr*>(peer_), sizeof *peer_) >= 0) ++stats_.sent;
    last_send_ms_ = packet.send_ms;
}

void Session::receive() {
    if (socket_ < 0) return;
    for (;;) {
        Packet packet{};
        sockaddr_in from{};
        socklen_t from_length = sizeof from;
        const ssize_t length = recvfrom(socket_, &packet, sizeof packet, 0, reinterpret_cast<sockaddr*>(&from), &from_length);
        if (length < 0) break;
        if (static_cast<size_t>(length) < offsetof(Packet, pads) || packet.magic != kMagic) { ++stats_.rejected; continue; }
        if (from.sin_addr.s_addr != peer_->sin_addr.s_addr) { ++stats_.rejected; continue; }
        if (packet.version != kProtocolVersion || packet.build != config_.build) {
            if (!stats_.connected) log("netplay: peer runs a different build; both devices need the same release");
            ++stats_.rejected;
            continue;
        }
        if (packet.player == config_.local_player) { ++stats_.rejected; continue; } // our own echo or a misconfigured peer
        // Adopt the peer's source port: a NAT or an ephemeral bind on the other side moves it.
        peer_->sin_port = from.sin_port;
        handle(packet, static_cast<size_t>(length));
    }
}

void Session::handle(const Packet& packet, size_t length) {
    ++stats_.received;
    last_receive_ms_ = now_ms();
    if (!stats_.connected) {
        stats_.connected = true;
        log("netplay: peer connected (player " + std::to_string(packet.player + 1) + ")");
    }
    if (packet.type == kKeepalive) {
        ++stats_.keepalives;
        return;
    }
    last_remote_send_ms_ = packet.send_ms;
    if (packet.echo_ms != 0) {
        const uint32_t now = now_ms();
        if (now >= packet.echo_ms) stats_.ping_ms = now - packet.echo_ms;
    }
    if (packet.type == kBye) {
        log("netplay: peer left the session");
        stats_.lost = true;
        return;
    }
    if (packet.ack > stats_.remote_ack) stats_.remote_ack = packet.ack;
    if (packet.type == kInput && packet.count > 0 && packet.count <= kRedundancy &&
        length >= offsetof(Packet, pads) + packet.count * sizeof(Pad)) {
        for (unsigned i = 0; i < packet.count; ++i) {
            const uint32_t index = packet.last_index - (packet.count - 1 - i);
            if (packet.last_index + 1 < packet.count) continue; // underflow guard
            // Only inputs we still need and that fit the ring (the peer cannot run further ahead
            // than the ring allows because it waits for our inputs too).
            if (any_remote_ && index <= remote_ack_) continue;
            if (index >= next_poll_ + kRing / 2) { ++stats_.rejected; continue; }
            Slot& s = remote_[index % kRing];
            s.index = index;
            s.pad = packet.pads[i];
            s.valid = true;
        }
        // Advance the contiguous mark.
        while (has_remote(any_remote_ ? remote_ack_ + 1 : 0)) {
            remote_ack_ = any_remote_ ? remote_ack_ + 1 : 0;
            any_remote_ = true;
        }
    }
    if (packet.hash_frame != 0) {
        stats_.last_remote_hash_frame = packet.hash_frame;
        const HashSlot& mine = my_hash_[packet.hash_frame % kRing];
        if (mine.valid && mine.frame == packet.hash_frame) {
            compare_hash(packet.hash_frame, mine.hash, packet.hash);
        } else if (packet.hash_frame >= latest_hash_frame_) {
            HashSlot& pending = remote_hash_[packet.hash_frame % kRing];
            pending = {packet.hash_frame, true, packet.hash};
        }
    }
}

void Session::compare_hash(uint32_t frame, uint32_t local, uint32_t remote) {
    if (local == remote || stats_.desync) return;
    stats_.desync = true;
    stats_.desync_frame = frame;
    stats_.desync_local = local;
    stats_.desync_remote = remote;
    char text[128];
    snprintf(text, sizeof text, "netplay: DESYNC at frame %u: local %08x remote %08x", frame, local, remote);
    log(text);
}

void Session::frame_hash(uint32_t frame, uint32_t hash) {
    if (frame == 0) return;
    my_hash_[frame % kRing] = {frame, true, hash};
    latest_hash_frame_ = frame;
    latest_hash_ = hash;
    HashSlot& pending = remote_hash_[frame % kRing];
    if (pending.valid && pending.frame == frame) {
        compare_hash(frame, hash, pending.hash);
        pending.valid = false;
    }
}

bool Session::wait_ready() {
    if (socket_ < 0) return false;
    const auto start = Clock::now();
    uint32_t last_ready_ms = 0;
    log("netplay: waiting for the other player...");
    while (!stats_.connected) {
        receive();
        if (stats_.connected) break;
        const uint32_t now = now_ms();
        if (now - last_ready_ms >= 100) { send_packet(kReady); last_ready_ms = now; }
        if (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count() >
            static_cast<long>(config_.ready_timeout_ms)) {
            log("netplay: nobody connected in time");
            stats_.lost = true;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // Let the peer learn we are here too, so its own wait ends quickly.
    send_packet(kReady);
    return true;
}

bool Session::poll(const Pad& local, Pad out[4]) {
    for (int i = 0; i < 4; ++i) { out[i] = Pad{}; out[i].err = -1; }
    if (socket_ < 0 || stats_.lost) {
        out[config_.local_player] = local;
        return false;
    }
    if (!stats_.connected && !wait_ready()) {
        out[config_.local_player] = local;
        return false;
    }
    const uint32_t p = next_poll_++;
    ++stats_.polls;
    const uint32_t scheduled = p + config_.delay;
    local_[scheduled % kRing] = {scheduled, true, local};
    latest_local_ = scheduled;
    send_packet(kInput);

    Pad mine{};
    if (p >= config_.delay) {
        const Slot& s = local_[p % kRing];
        if (s.valid && s.index == p) mine = s.pad;
    }
    const auto wait_start = Clock::now();
    bool waited = false;
    uint32_t last_resend = now_ms();
    while (!has_remote(p)) {
        receive();
        if (has_remote(p) || stats_.lost) break;
        waited = true;
        const uint32_t now = now_ms();
        if (now - last_resend >= 30) { send_packet(kInput); last_resend = now; }
        // A peer in a loading screen sends no inputs for a long time but keeps sending keepalives;
        // only silence ends the session.
        const uint32_t waited_ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - wait_start).count());
        if (waited_ms > config_.stall_timeout_ms && now - last_receive_ms_ > config_.stall_timeout_ms) {
            log("netplay: connection lost (nothing from the other player for " +
                std::to_string(now - last_receive_ms_) + " ms)");
            stats_.lost = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    if (waited) {
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - wait_start).count();
        if (ms > 2.0) { ++stats_.stalls; stats_.stall_ms += ms; if (ms > stats_.worst_stall_ms) stats_.worst_stall_ms = ms; }
    }
    out[config_.local_player] = mine;
    if (has_remote(p)) out[remote_player()] = remote_[p % kRing].pad;
    else if (stats_.lost) { out[remote_player()] = Pad{}; out[remote_player()].err = -1; }
    return !stats_.lost;
}

void Session::send_bye() {
    if (socket_ < 0) return;
    for (int i = 0; i < 3; ++i) send_packet(kBye);
}

} // namespace netplay
