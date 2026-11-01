#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include "../common/hw.h"
#include "../common/proto.h"

namespace {

constexpr const char* kLauncherComm = "screenpadctl-la";

struct Reply { std::string line; bool ok = false; };

int connectDaemon() {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, proto::socketPath(), sizeof addr.sun_path - 1);
    if (fd < 0 || ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) { if (fd >= 0) ::close(fd); return -1; }
    return fd;
}

std::optional<Reply> request(const std::string& cmd, bool stream = false) {
    int fd = connectDaemon();
    if (fd < 0) return std::nullopt;
    const std::string line = cmd + "\n";
    if (::write(fd, line.data(), line.size()) < 0) { ::close(fd); return std::nullopt; }
    std::string buf;
    char tmp[512];
    std::optional<Reply> reply;
    for (;;) {
        ssize_t n = ::read(fd, tmp, sizeof tmp);
        if (n <= 0) break;
        buf.append(tmp, static_cast<size_t>(n));
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string l = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            const bool isEvent = l.rfind("event ", 0) == 0;
            if (!isEvent && !reply) reply = Reply{l, l.rfind("ok", 0) == 0};
            if (stream) { std::puts(l.c_str()); std::fflush(stdout); }
            if (reply && !stream) { ::close(fd); return reply; }
        }
    }
    ::close(fd);
    return reply;
}

std::string field(const std::string& line, const std::string& key) {
    const std::string k = key + "=";
    size_t p = line.find(k);
    if (p == std::string::npos) return {};
    p += k.size();
    return line.substr(p, line.find(' ', p) - p);
}

std::vector<pid_t> launcherPids() {
    std::vector<pid_t> v;
    DIR* d = ::opendir("/proc");
    if (!d) return v;
    while (dirent* e = ::readdir(d)) {
        const pid_t pid = static_cast<pid_t>(std::atoi(e->d_name));
        if (pid <= 0 || pid == ::getpid()) continue;
        std::ifstream f(std::string("/proc/") + e->d_name + "/comm");
        std::string comm;
        if (std::getline(f, comm) && comm == kLauncherComm) v.push_back(pid);
    }
    ::closedir(d);
    return v;
}

void stopLauncher() {
    auto pids = launcherPids();
    for (pid_t p : pids) ::kill(p, SIGTERM);
    for (int i = 0; i < 30 && !launcherPids().empty(); ++i) ::usleep(100000);
    for (pid_t p : launcherPids()) ::kill(p, SIGKILL);
}

enum class Mode { Trackpad, Screenpad, Off };
const char* name(Mode m) { return m == Mode::Trackpad ? "trackpad" : m == Mode::Screenpad ? "screenpad" : "off"; }

std::optional<Mode> current() {
    if (!launcherPids().empty()) return Mode::Screenpad;
    const auto r = request("info");
    if (!r) return std::nullopt;
    return field(r->line, "last") == "off" ? Mode::Off : Mode::Trackpad;
}

int setMode(Mode m) {
    if (!request(std::string("lastmode ") + name(m))) {
        std::fputs("daemon unreachable (systemctl status screenpadctld)\n", stderr);
        return 1;
    }
    if (m != Mode::Screenpad) stopLauncher();
    std::puts(name(m));
    return 0;
}

const char* const kUsage =
    "usage: screenpadctl trackpad|screenpad|off|cycle|status\n"
    "       screenpadctl brightness [1-255]\n"
    "       screenpadctl daemon <command>\n";

int usage() {
    std::fputs(kUsage, stderr);
    return 2;
}

}

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "status";

    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        std::fputs(kUsage, stdout);
        return 0;
    }
    if (cmd == "trackpad") return setMode(Mode::Trackpad);
    if (cmd == "screenpad" || cmd == "on") return setMode(Mode::Screenpad);
    if (cmd == "off") return setMode(Mode::Off);
    if (cmd == "status" || cmd == "cycle") {
        const auto m = current();
        if (!m) { std::fputs("daemon unreachable (systemctl status screenpadctld)\n", stderr); return 1; }
        if (cmd == "status") { std::puts(name(*m)); return 0; }
        return setMode(*m == Mode::Trackpad ? Mode::Screenpad : *m == Mode::Screenpad ? Mode::Off : Mode::Trackpad);
    }

    if (cmd == "brightness") {
        if (argc < 3) {
            if (auto r = request("info")) { const std::string b = field(r->line, "brightness"); if (!b.empty()) { std::puts(b.c_str()); return 0; } }
            if (::geteuid() == 0) if (auto dir = hw::debugfsDir()) { hw::Wmi w(*dir); if (auto b = hw::brightness(w)) { std::printf("%d\n", *b); return 0; } }
            std::fputs("brightness unreadable (daemon unreachable)\n", stderr);
            return 1;
        }
        char* end = nullptr;
        const long v = std::strtol(argv[2], &end, 10);
        if (*end || v < 1 || v > 255) { std::fputs("brightness: integer from 1 to 255\n", stderr); return 2; }
        if (auto r = request("brightness " + std::to_string(v))) return r->ok ? 0 : 1;
        if (::geteuid() == 0) if (auto dir = hw::debugfsDir()) { hw::Wmi w(*dir); return hw::setBrightness(w, static_cast<int>(v)) ? 0 : 1; }
        std::fputs("daemon unreachable\n", stderr);
        return 1;
    }

    if (cmd == "daemon") {
        std::string line;
        for (int i = 2; i < argc; ++i) { if (i > 2) line += ' '; line += argv[i]; }
        if (line.empty()) return usage();
        auto r = request(line, line.rfind("watch on", 0) == 0);
        if (!r) { std::fputs("daemon unreachable (systemctl status screenpadctld)\n", stderr); return 1; }
        if (line.rfind("watch on", 0) != 0) std::puts(r->line.c_str());
        return r->ok ? 0 : 1;
    }
    return usage();
}
