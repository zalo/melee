// Native-to-native online play: the runtime side of netplay_session.h.
//
// Two devices running the same release meet on the LAN (UDP beacons + a TCP handshake started from
// the Online Play screen), agree on a seed, an input delay and the host's save, then both relaunch
// the game with the session in the environment (MELEE_ONLINE_*). From the first pad poll on, every
// PADRead goes through the lockstep session, so both games see identical inputs at identical polls;
// the per-frame simulation digest travels with the inputs and flags a desync on screen and in the log.
#include "include/melee_netplay.h"
#include "include/melee_settings.h"
#include "netplay_session.h"

#include <dolphin/os.h>

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr uint16_t kDiscoveryPort = 26261; // UDP beacons from hosts
constexpr uint16_t kControlPort = 26262;   // TCP handshake on the host
constexpr uint16_t kInputPortHost = 26263; // UDP lockstep
constexpr uint16_t kInputPortGuest = 26264;
constexpr uint32_t kHelloMagic = 0x484E4C4Du; // "MLNH"
constexpr uint32_t kAckMagic = 0x5941444Fu;   // "OKAY"
constexpr uint8_t kHandshakeVersion = 1;

void logf(const char* format, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::fprintf(stderr, "[netplay] ");
    std::vfprintf(stderr, format, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

// 32-bit FNV-1a over the running executable: both peers must run the same release.
uint32_t build_digest() {
    static const uint32_t digest = [] {
        uint32_t h = 2166136261u;
        std::ifstream exe("/proc/self/exe", std::ios::binary);
        std::vector<char> chunk(1 << 20);
        while (exe) {
            exe.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto n = exe.gcount();
            for (std::streamsize i = 0; i < n; ++i) { h ^= static_cast<uint8_t>(chunk[i]); h *= 16777619u; }
        }
        return h ? h : 1u;
    }();
    return digest;
}

std::vector<std::string> local_addresses() {
    std::vector<std::string> out;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP)) continue;
        char text[INET_ADDRSTRLEN];
        const auto* in = reinterpret_cast<sockaddr_in*>(a->ifa_addr);
        if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof text)) out.emplace_back(text);
    }
    freeifaddrs(list);
    return out;
}

std::string local_address() {
    std::string best;
    for (const auto& text : local_addresses()) {
        // Prefer a Wi-Fi/Ethernet address over the Flip's USB gadget link-local one.
        if (best.empty() || text.rfind("169.254.", 0) != 0) best = text;
        if (text.rfind("169.254.", 0) != 0) break;
    }
    return best;
}

bool is_local_address(const std::string& ip) {
    for (const auto& text : local_addresses()) if (text == ip) return true;
    return false;
}

std::string device_name() {
    char name[64] = {};
    if (gethostname(name, sizeof name - 1) != 0 || !name[0]) return "melee";
    return name;
}

const char* env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return v && *v ? v : fallback;
}

// ---- the in-game session ----------------------------------------------------------------------
struct Runtime {
    bool active = false;
    bool host = false;
    int local_port = 0;
    unsigned delay = 3;
    std::string peer;
    std::string user_path;
    netplay::Session session;
    bool opened = false;
    std::string overlay;
    bool overlay_warning = false;
    Clock::time_point started = Clock::now();
} rt;

void configure_from_env() {
    const char* role = std::getenv("MELEE_ONLINE_ROLE");
    if (!role || !*role) return;
    rt.active = true;
    rt.host = std::strcmp(role, "host") == 0;
    rt.local_port = std::atoi(env_or("MELEE_ONLINE_PORT", rt.host ? "0" : "1")) != 0 ? 1 : 0;
    rt.delay = static_cast<unsigned>(std::strtoul(env_or("MELEE_ONLINE_DELAY", "3"), nullptr, 10));
    if (rt.delay < 1) rt.delay = 1;
    if (rt.delay > 15) rt.delay = 15;
    rt.peer = env_or("MELEE_ONLINE_PEER", "");
}

bool open_session() {
    if (rt.opened) return true;
    if (rt.peer.empty()) { logf("MELEE_ONLINE_PEER is not set; playing offline"); rt.active = false; return false; }
    netplay::Config config;
    config.local_player = rt.local_port;
    config.delay = rt.delay;
    config.peer_ip = rt.peer;
    config.local_udp_port = static_cast<uint16_t>(std::strtoul(env_or("MELEE_ONLINE_UDP_LOCAL", rt.local_port == 0 ? "26263" : "26264"), nullptr, 10));
    config.peer_udp_port = static_cast<uint16_t>(std::strtoul(env_or("MELEE_ONLINE_UDP_PEER", rt.local_port == 0 ? "26264" : "26263"), nullptr, 10));
    config.build = build_digest();
    config.ready_timeout_ms = static_cast<unsigned>(std::strtoul(env_or("MELEE_ONLINE_READY_TIMEOUT_MS", "90000"), nullptr, 10));
    config.stall_timeout_ms = static_cast<unsigned>(std::strtoul(env_or("MELEE_ONLINE_STALL_TIMEOUT_MS", "10000"), nullptr, 10));
    config.test_loss_percent = static_cast<unsigned>(std::strtoul(env_or("MELEE_ONLINE_TEST_LOSS", "0"), nullptr, 10));
    if (config.test_loss_percent) logf("TEST: dropping %u%% of outgoing input packets", config.test_loss_percent);
    config.log = [](const std::string& text) { std::fprintf(stderr, "[%s]\n", text.c_str()); };
    if (!rt.session.open(config)) { rt.active = false; return false; }
    rt.opened = true;
    logf("session: %s, player %d, delay %u, build %08x", rt.host ? "host" : "guest", rt.local_port + 1, rt.delay, config.build);
    return true;
}

// ---- lobby: discovery and handshake, on a background thread while the menu is open --------------
struct HostEntry {
    std::string name, ip;
    uint16_t control_port = kControlPort;
    uint32_t build = 0;
    Clock::time_point seen;
};

struct SessionPlan {
    bool valid = false;
    bool host = false;
    std::string peer;
    uint32_t seed = 0;
    unsigned delay = 3;
    std::string card_dir; // guest: the copied save
    int debug_overlays = 0, debug_level = 0; // the host's, applied on both sides
};

class Lobby {
public:
    void enter();      // listen for beacons
    void leave();
    void host();
    void join(int index);
    bool hosting() const { return hosting_; }
    std::vector<HostEntry> hosts() const { std::lock_guard lock(mutex_); return hosts_; }
    std::string status() const { std::lock_guard lock(mutex_); return status_; }
    SessionPlan take_plan() { std::lock_guard lock(mutex_); SessionPlan p = plan_; return p; }

private:
    void run();
    void set_status(const std::string& s) { std::lock_guard lock(mutex_); status_ = s; logf("%s", s.c_str()); }
    void send_beacons();
    void read_beacons();
    void accept_guest();
    void connect_to(const HostEntry& target);
    bool handshake_host(int fd, const std::string& guest_ip);
    bool handshake_guest(int fd, const HostEntry& target);
    static bool send_all(int fd, const void* data, size_t size);
    static bool recv_all(int fd, void* data, size_t size);

    std::thread thread_;
    std::atomic_bool running_{false};
    std::atomic_bool hosting_{false};
    std::atomic_int join_request_{-1};
    mutable std::mutex mutex_;
    std::vector<HostEntry> hosts_;
    std::string status_;
    SessionPlan plan_;
    int beacon_in_ = -1, beacon_out_ = -1, listen_ = -1;
    Clock::time_point last_beacon_{};
} lobby;

#pragma pack(push, 1)
struct Hello { uint32_t magic; uint8_t version; uint8_t reserved[3]; uint32_t build; char name[32]; };
// Port settings that change gameplay code paths travel with the session: the two-device test
// diverged on the title's first frame because one side had the debug overlays on.
struct SessionHeader { uint32_t seed; uint8_t delay; uint8_t guest_port; uint16_t file_count; uint8_t debug_overlays; uint8_t debug_level; uint8_t reserved[2]; };
struct FileHeader { uint16_t name_length; uint16_t reserved; uint32_t size; };
#pragma pack(pop)

bool Lobby::send_all(int fd, const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size) {
        const ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
        if (n <= 0) return false;
        p += n; size -= static_cast<size_t>(n);
    }
    return true;
}
bool Lobby::recv_all(int fd, void* data, size_t size) {
    char* p = static_cast<char*>(data);
    while (size) {
        const ssize_t n = recv(fd, p, size, 0);
        if (n <= 0) return false;
        p += n; size -= static_cast<size_t>(n);
    }
    return true;
}

void Lobby::enter() {
    if (running_) return;
    running_ = true;
    join_request_ = -1;
    { std::lock_guard lock(mutex_); hosts_.clear(); plan_ = {}; status_ = "Searching for hosts on this network..."; }
    thread_ = std::thread([this] { run(); });
}

void Lobby::leave() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    hosting_ = false;
}

void Lobby::host() {
    if (!running_) enter();
    if (hosting_) return;
    hosting_ = true;
    set_status("Hosting: waiting for a player. This device is " + local_address());
}

void Lobby::join(int index) {
    if (!running_) return;
    join_request_ = index;
}

void Lobby::run() {
    // Beacon listener (guest side) and sender (host side) share the discovery port.
    beacon_in_ = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    setsockopt(beacon_in_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    any.sin_port = htons(kDiscoveryPort);
    if (bind(beacon_in_, reinterpret_cast<sockaddr*>(&any), sizeof any) < 0) logf("beacon bind: %s", strerror(errno));
    fcntl(beacon_in_, F_SETFL, fcntl(beacon_in_, F_GETFL) | O_NONBLOCK);
    beacon_out_ = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(beacon_out_, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);

    while (running_) {
        if (hosting_ && listen_ < 0) {
            listen_ = socket(AF_INET, SOCK_STREAM, 0);
            setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons(kControlPort);
            if (bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(listen_, 1) < 0) {
                set_status(std::string("Cannot host: ") + strerror(errno));
                close(listen_); listen_ = -1; hosting_ = false;
            } else {
                fcntl(listen_, F_SETFL, fcntl(listen_, F_GETFL) | O_NONBLOCK);
            }
        }
        if (hosting_) send_beacons();
        read_beacons();
        if (hosting_ && listen_ >= 0) accept_guest();
        const int request = join_request_.exchange(-1);
        if (request >= 0) {
            HostEntry target;
            bool found = false;
            { std::lock_guard lock(mutex_); if (request < static_cast<int>(hosts_.size())) { target = hosts_[request]; found = true; } }
            if (found) connect_to(target);
        }
        // Forget hosts that stopped announcing.
        {
            std::lock_guard lock(mutex_);
            const auto now = Clock::now();
            hosts_.erase(std::remove_if(hosts_.begin(), hosts_.end(), [&](const HostEntry& h) {
                return now - h.seen > std::chrono::seconds(4);
            }), hosts_.end());
        }
        pollfd fds[2] = {{beacon_in_, POLLIN, 0}, {listen_, POLLIN, 0}};
        poll(fds, listen_ >= 0 ? 2 : 1, 150);
    }
    if (listen_ >= 0) close(listen_);
    close(beacon_in_);
    close(beacon_out_);
    listen_ = beacon_in_ = beacon_out_ = -1;
}

void Lobby::send_beacons() {
    const auto now = Clock::now();
    if (now - last_beacon_ < std::chrono::milliseconds(500)) return;
    last_beacon_ = now;
    char text[160];
    std::snprintf(text, sizeof text, "MLNP1 %08x %u %s", build_digest(), kControlPort, device_name().c_str());
    // Every interface's broadcast address, plus the limited broadcast.
    std::vector<in_addr_t> targets{htonl(INADDR_BROADCAST)};
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* a = list; a; a = a->ifa_next)
            if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && a->ifa_broadaddr && (a->ifa_flags & IFF_BROADCAST) && !(a->ifa_flags & IFF_LOOPBACK))
                targets.push_back(reinterpret_cast<sockaddr_in*>(a->ifa_broadaddr)->sin_addr.s_addr);
        freeifaddrs(list);
    }
    for (in_addr_t target : targets) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = target;
        to.sin_port = htons(kDiscoveryPort);
        sendto(beacon_out_, text, std::strlen(text), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
    }
}

void Lobby::read_beacons() {
    for (;;) {
        char text[256];
        sockaddr_in from{};
        socklen_t from_length = sizeof from;
        const ssize_t n = recvfrom(beacon_in_, text, sizeof text - 1, 0, reinterpret_cast<sockaddr*>(&from), &from_length);
        if (n <= 0) break;
        text[n] = '\0';
        unsigned build = 0, port = 0;
        char name[64] = {};
        if (std::sscanf(text, "MLNP1 %x %u %63s", &build, &port, name) != 3) continue;
        char ip[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip)) continue;
        if (is_local_address(ip)) continue; // our own announcement, on any of our interfaces
        std::lock_guard lock(mutex_);
        bool updated = false;
        for (auto& h : hosts_)
            if (h.ip == ip) { h.name = name; h.build = build; h.control_port = static_cast<uint16_t>(port); h.seen = Clock::now(); updated = true; }
        if (!updated && hosts_.size() < 8) {
            hosts_.push_back({name, ip, static_cast<uint16_t>(port), build, Clock::now()});
            logf("found host %s at %s (build %08x%s)", name, ip, build, build == build_digest() ? "" : ", DIFFERENT RELEASE");
        }
    }
}

void Lobby::accept_guest() {
    sockaddr_in from{};
    socklen_t from_length = sizeof from;
    const int fd = accept(listen_, reinterpret_cast<sockaddr*>(&from), &from_length);
    if (fd < 0) return;
    char ip[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
    timeval timeout{10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    set_status(std::string("Player joining from ") + ip + "...");
    const bool ok = handshake_host(fd, ip);
    close(fd);
    if (ok) set_status(std::string("Connected to ") + ip + ". Starting...");
}

void Lobby::connect_to(const HostEntry& target) {
    if (target.build != build_digest()) { set_status("That host runs a different release; update both devices."); return; }
    set_status("Joining " + target.name + " at " + target.ip + "...");
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    timeval timeout{10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target.control_port);
    inet_pton(AF_INET, target.ip.c_str(), &addr.sin_addr);
    // Bounded connect: non-blocking + poll, then back to blocking with the timeouts above.
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int result = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    if (result < 0 && errno == EINPROGRESS) {
        pollfd p{fd, POLLOUT, 0};
        if (poll(&p, 1, 5000) <= 0) result = -1;
        else { int err = 0; socklen_t len = sizeof err; getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len); result = err ? -1 : 0; }
    }
    if (result < 0) { set_status("Could not reach " + target.name + "."); close(fd); return; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    const bool ok = handshake_guest(fd, target);
    close(fd);
    if (ok) set_status("Connected to " + target.name + ". Starting...");
}

std::string host_card_dir(const std::string& user_path) { return user_path + "/USA/Card A"; }
std::string guest_card_dir(const std::string& user_path) { return user_path + "/USA/Card A.online"; }

bool Lobby::handshake_host(int fd, const std::string& guest_ip) {
    Hello hello{kHelloMagic, kHandshakeVersion, {}, build_digest(), {}};
    std::strncpy(hello.name, device_name().c_str(), sizeof hello.name - 1);
    Hello theirs{};
    if (!recv_all(fd, &theirs, sizeof theirs) || theirs.magic != kHelloMagic) { set_status("Handshake failed: bad hello."); return false; }
    if (!send_all(fd, &hello, sizeof hello)) { set_status("Handshake failed: connection dropped."); return false; }
    if (theirs.version != kHandshakeVersion || theirs.build != build_digest()) { set_status("That player runs a different release; update both devices."); return false; }
    // The session: a fresh seed, this device's input-delay setting, and its save so both menus agree.
    uint32_t seed = 0;
    if (std::ifstream random("/dev/urandom", std::ios::binary); random) random.read(reinterpret_cast<char*>(&seed), sizeof seed);
    if (!seed) seed = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()) | 1u;
    unsigned delay = static_cast<unsigned>(MeleeNativeSettingsData.online_input_delay);
    if (delay < 1) delay = 1;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(host_card_dir(rt.user_path), ec))
        if (entry.is_regular_file()) files.push_back(entry.path());
    SessionHeader header{seed, static_cast<uint8_t>(delay), 1, static_cast<uint16_t>(files.size()),
                         static_cast<uint8_t>(MeleeNativeSettingsData.debug_overlays != 0),
                         static_cast<uint8_t>(MeleeNativeSettingsData.debug_level), {}};
    if (!send_all(fd, &header, sizeof header)) return false;
    for (const auto& path : files) {
        const std::string name = path.filename().string();
        std::ifstream in(path, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        FileHeader fh{static_cast<uint16_t>(name.size()), 0, static_cast<uint32_t>(data.size())};
        if (!send_all(fd, &fh, sizeof fh) || !send_all(fd, name.data(), name.size()) || !send_all(fd, data.data(), data.size())) {
            set_status("Handshake failed while sending the save."); return false;
        }
    }
    uint32_t ack = 0;
    if (!recv_all(fd, &ack, sizeof ack) || ack != kAckMagic) { set_status("Handshake failed: no acknowledgement."); return false; }
    logf("host handshake done: guest %s (%s), seed %u, delay %u, %zu save files", theirs.name, guest_ip.c_str(), seed, delay, files.size());
    std::lock_guard lock(mutex_);
    plan_ = {true, true, guest_ip, seed, delay, {}, header.debug_overlays, header.debug_level};
    return true;
}

bool Lobby::handshake_guest(int fd, const HostEntry& target) {
    Hello hello{kHelloMagic, kHandshakeVersion, {}, build_digest(), {}};
    std::strncpy(hello.name, device_name().c_str(), sizeof hello.name - 1);
    if (!send_all(fd, &hello, sizeof hello)) { set_status("Handshake failed: connection dropped."); return false; }
    Hello theirs{};
    if (!recv_all(fd, &theirs, sizeof theirs) || theirs.magic != kHelloMagic) { set_status("Handshake failed: bad hello."); return false; }
    if (theirs.version != kHandshakeVersion || theirs.build != build_digest()) { set_status("That host runs a different release; update both devices."); return false; }
    SessionHeader header{};
    if (!recv_all(fd, &header, sizeof header)) { set_status("Handshake failed: no session."); return false; }
    // The host's save replaces ours for this session only, in a directory of its own.
    const fs::path dir = guest_card_dir(rt.user_path);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    for (unsigned i = 0; i < header.file_count; ++i) {
        FileHeader fh{};
        if (!recv_all(fd, &fh, sizeof fh) || fh.name_length == 0 || fh.name_length > 255 || fh.size > (64u << 20)) { set_status("Handshake failed: bad save file."); return false; }
        std::string name(fh.name_length, '\0');
        std::vector<char> data(fh.size);
        if (!recv_all(fd, name.data(), name.size()) || (fh.size && !recv_all(fd, data.data(), data.size()))) { set_status("Handshake failed while receiving the save."); return false; }
        if (name.find('/') != std::string::npos || name == "." || name == "..") { set_status("Handshake failed: bad save name."); return false; }
        std::ofstream out(dir / name, std::ios::binary);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    const uint32_t ack = kAckMagic;
    if (!send_all(fd, &ack, sizeof ack)) return false;
    logf("guest handshake done: host %s (%s), seed %u, delay %u, %u save files", theirs.name, target.ip.c_str(), header.seed, header.delay, header.file_count);
    std::lock_guard lock(mutex_);
    plan_ = {true, false, target.ip, header.seed, header.delay, dir.string(), header.debug_overlays, header.debug_level};
    return true;
}

std::atomic_bool relaunch_pending{false};
std::string menu_status_cache, local_address_cache;
std::vector<std::string> host_labels_cache;
} // namespace

extern "C" {

int MeleeNativeNetplayActive(void) { return rt.active; }
int MeleeNativeNetplayLocalPort(void) { return rt.local_port; }
int MeleeNativeNetplayRelaunchPending(void) { return relaunch_pending; }

void MeleeNativeNetplayInit(const char* user_path) {
    rt.user_path = user_path ? user_path : "";
    configure_from_env();
    if (rt.active) {
        // The intro movie is skipped and disc/ARAM I/O completes deterministically; see gmopening.c
        // and MeleeNativeDeterministicIO().
        open_session();
    }
}

// From MeleeNativePADRead: output holds the four 12-byte pads as the game will see them, with the
// local player's pad (physical controller + keyboard/script) in port 0.
int MeleeNativeNetplayPads(void* output) {
    if (!rt.active || !rt.opened) return 0;
    netplay::Pad pads[4];
    std::memcpy(pads, output, sizeof pads);
    netplay::Pad local = pads[0];
    local.err = 0;
    local.reserved = 0;
    netplay::Pad out[4];
    rt.session.poll(local, out);
    std::memcpy(output, out, sizeof out);
    // MELEE_TRACE_PADS=lo-hi: the four pads applied at polls lo..hi, to diff the two sides byte for byte.
    static int trace_lo = -1, trace_hi = -1;
    if (trace_lo < 0) {
        trace_lo = trace_hi = 0;
        if (const char* v = std::getenv("MELEE_TRACE_PADS")) std::sscanf(v, "%d-%d", &trace_lo, &trace_hi);
    }
    const unsigned poll = rt.session.stats().polls;
    if (trace_hi > 0 && poll >= static_cast<unsigned>(trace_lo) && poll <= static_cast<unsigned>(trace_hi)) {
        char text[160];
        int n = std::snprintf(text, sizeof text, "[pad-trace] poll=%u", poll);
        for (int port = 0; port < 4; ++port) {
            const auto* b = reinterpret_cast<const unsigned char*>(&out[port]);
            n += std::snprintf(text + n, sizeof text - n, " P%d=", port + 1);
            for (int i = 0; i < 12; ++i) n += std::snprintf(text + n, sizeof text - n, "%02x", b[i]);
        }
        std::fprintf(stderr, "%s\n", text);
    }
    return 1;
}

void MeleeNativeNetplayFrameHash(unsigned frame, unsigned hash) {
    if (rt.active && rt.opened) rt.session.frame_hash(frame, hash);
}

const char* MeleeNativeNetplayOverlayText(void) {
    if (!rt.active || !rt.opened) return nullptr;
    const auto& s = rt.session.stats();
    char text[200];
    if (s.lost && !s.connected)
        std::snprintf(text, sizeof text, "ONLINE: the other player did not connect; playing offline");
    else if (s.lost)
        std::snprintf(text, sizeof text, "ONLINE: connection lost after %u polls; playing offline", s.polls);
    else if (s.desync)
        std::snprintf(text, sizeof text, "ONLINE P%d: DESYNC at frame %u (local %08x, remote %08x)", rt.local_port + 1,
                      s.desync_frame, s.desync_local, s.desync_remote);
    else if (!s.connected)
        std::snprintf(text, sizeof text, "ONLINE P%d: waiting for the other player (%s)...", rt.local_port + 1, rt.peer.c_str());
    else
        std::snprintf(text, sizeof text, "ONLINE P%d  delay %u  ping %u ms  stalls %u (%.0f ms, worst %.0f)", rt.local_port + 1,
                      rt.delay, s.ping_ms, s.stalls, s.stall_ms, s.worst_stall_ms);
    rt.overlay = text;
    rt.overlay_warning = s.lost || s.desync;
    return rt.overlay.c_str();
}
int MeleeNativeNetplayOverlayIsWarning(void) { return rt.overlay_warning; }

// For the periodic [perf] block in log.txt.
const char* MeleeNativeNetplayStatsLine(void) {
    if (!rt.active || !rt.opened) return nullptr;
    static char text[240];
    const auto& s = rt.session.stats();
    std::snprintf(text, sizeof text,
                  "[netplay-stats] player=%d connected=%d lost=%d polls=%u stalls=%u stall_ms=%.0f worst_stall_ms=%.0f ping_ms=%u "
                  "sent=%u received=%u keepalives=%u rejected=%u remote_ack=%u desync=%d desync_frame=%u",
                  rt.local_port + 1, s.connected, s.lost, s.polls, s.stalls, s.stall_ms, s.worst_stall_ms, s.ping_ms, s.sent,
                  s.received, s.keepalives, s.rejected, s.remote_ack, s.desync, s.desync_frame);
    return text;
}

void MeleeNativeNetplayMenuEnter(void) { lobby.enter(); }
void MeleeNativeNetplayMenuLeave(void) { lobby.leave(); }
void MeleeNativeNetplayHost(void) { lobby.host(); }
int MeleeNativeNetplayHosting(void) { return lobby.hosting(); }
int MeleeNativeNetplayHostCount(void) { return static_cast<int>(lobby.hosts().size()); }
const char* MeleeNativeNetplayHostLabel(int index) {
    const auto hosts = lobby.hosts();
    host_labels_cache.resize(hosts.size());
    if (index < 0 || index >= static_cast<int>(hosts.size())) return "";
    host_labels_cache[index] = hosts[index].name + " " + hosts[index].ip + (hosts[index].build == build_digest() ? "" : " (other release)");
    return host_labels_cache[index].c_str();
}
void MeleeNativeNetplayJoin(int index) { lobby.join(index); }
const char* MeleeNativeNetplayMenuStatus(void) { menu_status_cache = lobby.status(); return menu_status_cache.c_str(); }
const char* MeleeNativeNetplayLocalAddress(void) { local_address_cache = local_address(); return local_address_cache.c_str(); }

void MeleeNativeNetplayMenuTick(void) {
    const SessionPlan plan = lobby.take_plan();
    if (!plan.valid) return;
    lobby.leave();
    // Both sides restart into the session: the same seed (the game's boot seed comes from
    // MELEE_TEST_SEED), the same delay, the host's save on the guest, lockstep from the first poll.
    setenv("MELEE_ONLINE_ROLE", plan.host ? "host" : "join", 1);
    setenv("MELEE_ONLINE_PEER", plan.peer.c_str(), 1);
    setenv("MELEE_ONLINE_PORT", plan.host ? "0" : "1", 1);
    setenv("MELEE_ONLINE_DELAY", std::to_string(plan.delay).c_str(), 1);
    setenv("MELEE_TEST_SEED", std::to_string(plan.seed).c_str(), 1);
    setenv("MELEE_DEBUG_OVERLAYS", plan.debug_overlays ? "1" : "0", 1);
    setenv("MELEE_DEBUG_LEVEL", std::to_string(plan.debug_level).c_str(), 1);
    setenv("MELEE_DEBUG_MENU", "0", 1);
    if (!plan.card_dir.empty()) setenv("AURORA_CARD_PATH_A", plan.card_dir.c_str(), 1);
    else unsetenv("AURORA_CARD_PATH_A");
    // Test harness: a scripted run that navigated the menu to start the session continues with the
    // session's own script after the relaunch.
    if (const char* next = std::getenv("MELEE_INPUT_SCRIPT_ONLINE"); next && *next) setenv("MELEE_INPUT_SCRIPT", next, 1);
    logf("relaunching into the session as %s with %s", plan.host ? "host" : "guest", plan.peer.c_str());
    relaunch_pending = true;
    OSResetSystem(0, 0, 0);
}

// A reset requested by the game itself leaves the session: the next boot is offline again.
void MeleeNativeNetplayClearEnvironment(void) {
    if (relaunch_pending) return;
    if (rt.active && rt.opened) rt.session.send_bye();
    for (const char* name : {"MELEE_ONLINE_ROLE", "MELEE_ONLINE_PEER", "MELEE_ONLINE_PORT", "MELEE_ONLINE_DELAY", "AURORA_CARD_PATH_A"})
        unsetenv(name);
    if (rt.active) for (const char* name : {"MELEE_TEST_SEED", "MELEE_DEBUG_OVERLAYS", "MELEE_DEBUG_LEVEL", "MELEE_DEBUG_MENU"}) unsetenv(name);
}

} // extern "C"
