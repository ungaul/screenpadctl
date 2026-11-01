#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include "../common/hw.h"
#include "../common/proto.h"
#include "session.h"
#include "touchreader.h"
#include "uinput.h"

namespace {

volatile sig_atomic_t gStop = 0;
void onSignal(int) { gStop = 1; }

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void logf(const char* fmt, auto... a) {
    std::fprintf(stderr, "screenpadctld: ");
    if constexpr (sizeof...(a) == 0) std::fputs(fmt, stderr);
    else std::fprintf(stderr, fmt, a...);
    std::fputc('\n', stderr);
}

struct Config {
    std::string touchName = "GDX1515:00 27C6:01F4";
    std::string i2c = "i2c-GDX1515:00";
    int rotation = 270;
    std::string lastMode = "trackpad";
    int brightness = 0;
    int leaseMs = 8000;
    std::string launcherScale;
    bool fake = false;
};

std::string stateFile() {
    const char* dir = std::getenv("STATE_DIRECTORY");
    return std::string(dir && *dir ? dir : "/var/lib/screenpadctl") + "/settings.conf";
}

void loadFile(Config& c, const std::string& path) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) { s.erase(0, s.find_first_not_of(" \t")); s.erase(s.find_last_not_of(" \t\r") + 1); return s; };
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "touch_name") c.touchName = v;
        else if (k == "i2c_id") c.i2c = v;
        else if (k == "rotation") c.rotation = std::atoi(v.c_str());
        else if (k == "mode" && (v == "trackpad" || v == "screenpad" || v == "off")) c.lastMode = v;
        else if (k == "brightness") c.brightness = std::atoi(v.c_str());
        else if (k == "lease_ms") c.leaseMs = std::atoi(v.c_str());
        else if (k == "launcher_scale") c.launcherScale = v;
    }
}

Config loadConfig() {
    Config c;
    const char* p = std::getenv("SCREENPADCTL_CONF");
    loadFile(c, p && *p ? p : "/etc/screenpadctl.conf");
    if (!c.fake) loadFile(c, stateFile());
    return c;
}

void saveSettings(const Config& c) {
    if (c.fake) return;
    ::mkdir("/var/lib/screenpadctl", 0755);
    std::ofstream f(stateFile());
    f << "brightness = " << c.brightness << "\n"
      << "mode = " << c.lastMode << "\n";
}

class Mode {
public:
    explicit Mode(const Config& c) : c_(c) {}
    bool isTouch() const { return touch_; }

    bool touch() {
        if (touch_) return true;
        if (!c_.fake) {
            ::mkdir("/run/udev", 0755);
            ::mkdir("/run/udev/rules.d", 0755);
            std::ofstream r(kRule);
            r << "SUBSYSTEM==\"input\", ATTRS{name}==\"" << c_.touchName << " Touchpad\", "
                 "ENV{ID_INPUT_TOUCHPAD}=\"0\", ENV{ID_INPUT_TOUCHSCREEN}=\"1\"\n";
            if (!r.good()) return false;
            r.close();
            reattach();
        }
        touch_ = true;
        return true;
    }

    void pointer() {
        const bool had = touch_ || ::access(kRule, F_OK) == 0;
        if (!c_.fake && had) { ::unlink(kRule); reattach(); }
        touch_ = false;
    }

private:
    void reattach() {
        if (std::system("udevadm control --reload-rules") != 0) logf("udevadm reload failed");
        const std::string drv = "/sys/bus/i2c/drivers/i2c_hid_acpi";
        hw::writeFile(drv + "/unbind", c_.i2c);
        hw::writeFile(drv + "/bind", c_.i2c);
        if (std::system("udevadm settle --timeout=3") != 0) logf("udevadm settle failed");
    }
    static constexpr const char* kRule = "/run/udev/rules.d/71-screenpadctl-touchscreen.rules";
    const Config& c_;
    bool touch_ = false;
};

struct Client {
    int fd = -1;
    std::string buf;
    bool lease = false;
    bool watch = false;
    int64_t deadline = 0;
};

class Daemon {
public:
    explicit Daemon(Config c)
        : cfg_(std::move(c)), mode_(cfg_),
          reader_({cfg_.touchName, cfg_.rotation}, [this](const Frame& f) { onFrame(f); }, [this](bool down) { onPhysicalButton(down); }) {}

    int run() {
        if (!cfg_.fake) {
            if (auto dir = hw::debugfsDir()) {
                wmi_ = std::make_unique<hw::Wmi>(*dir);
                applyPanelPower();
            } else {
                logf("asus-wmi debugfs unavailable: power and brightness disabled");
            }
        }
        mode_.pointer();
        refreshReader();

        if (!listen()) return 1;
        struct sigaction sa{};
        sa.sa_handler = onSignal;
        ::sigaction(SIGTERM, &sa, nullptr);
        ::sigaction(SIGINT, &sa, nullptr);
        ::signal(SIGPIPE, SIG_IGN);
        logf("ready (%s)%s", proto::socketPath(), cfg_.fake ? " [fake]" : "");

        while (!gStop) {
            std::vector<pollfd> pfds{{listenFd_, POLLIN, 0}};
            for (auto& [fd, c] : clients_) pfds.push_back({fd, POLLIN, 0});
            const size_t nClient = clients_.size();
            for (int fd : reader_.fds()) pfds.push_back({fd, POLLIN, 0});

            ::poll(pfds.data(), pfds.size(), 200);

            if (pfds[0].revents & POLLIN) acceptClient();
            for (size_t i = 0; i < nClient; ++i) {
                const pollfd& p = pfds[1 + i];
                if (p.revents & (POLLIN | POLLHUP | POLLERR)) readClient(p.fd);
            }
            for (size_t i = 1 + nClient; i < pfds.size(); ++i)
                if (pfds[i].revents & POLLIN) reader_.onReadable(pfds[i].fd);
            if (reader_.lost()) { logf("pad input device lost"); trackpadOff(true); refreshReader(); }

            checkResume();
            maintainLauncher();
            checkLeases();
            flushDrops();
        }
        logf("stopping: back to pointer mode");
        revertAll();
        ::unlink(proto::socketPath());
        return 0;
    }

private:
    bool listen() {
        listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, proto::socketPath(), sizeof addr.sun_path - 1);
        ::unlink(addr.sun_path);
        if (listenFd_ < 0 || ::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || ::listen(listenFd_, 8) < 0) {
            logf("socket %s failed: %s", addr.sun_path, std::strerror(errno));
            return false;
        }
        ::chmod(addr.sun_path, 0666);
        return true;
    }

    void acceptClient() {
        int fd = ::accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        ucred cred{};
        socklen_t len = sizeof cred;
        if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0 || !(cred.uid == 0 || cred.uid >= 1000)) { ::close(fd); return; }
        clients_[fd].fd = fd;
    }

    void readClient(int fd) {
        auto it = clients_.find(fd);
        if (it == clients_.end()) return;
        Client& c = it->second;
        char tmp[512];
        for (;;) {
            ssize_t n = ::read(fd, tmp, sizeof tmp);
            if (n > 0) { c.buf.append(tmp, static_cast<size_t>(n)); if (c.buf.size() > 4096) { drop_.push_back(fd); return; } continue; }
            if (n == 0 || (errno != EAGAIN && errno != EINTR)) { drop_.push_back(fd); return; }
            break;
        }
        size_t nl;
        while ((nl = c.buf.find('\n')) != std::string::npos) {
            std::string line = c.buf.substr(0, nl);
            c.buf.erase(0, nl + 1);
            send(c, handle(c, line));
        }
    }

    void send(Client& c, const std::string& msg) { const std::string m = msg + "\n"; ::send(c.fd, m.data(), m.size(), MSG_NOSIGNAL | MSG_DONTWAIT); }
    void sendFd(int fd, const std::string& msg) { auto it = clients_.find(fd); if (it != clients_.end()) send(it->second, msg); }

    std::string hyprName() const {
        std::string s = cfg_.touchName + " Touchpad";
        for (char& ch : s) ch = ch == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    }

    std::string state() {
        std::ostringstream o;
        o << "mode=" << (mode_.isTouch() ? "touch" : "pointer") << " hypr=" << hyprName();
        o << " last=" << cfg_.lastMode;
        if (wmi_) { o << " power=" << (hw::isOn(*wmi_) ? 1 : 0); if (auto b = hw::brightness(*wmi_)) o << " brightness=" << *b; }
        else if (cfg_.fake) o << " power=" << (fakePower_ ? 1 : 0) << " brightness=" << fakeBrightness_;
        return o.str();
    }

    bool holderExists(int except = -1) const {
        for (auto& [fd, c] : clients_) if (c.lease && fd != except) return true;
        return false;
    }

    std::string handle(Client& c, const std::string& line) {
        std::istringstream in(line);
        std::string cmd, a;
        in >> cmd;

        if (cmd == "info") return "ok " + state();

        if (cmd == "ping") {
            if (c.lease) c.deadline = nowMs() + cfg_.leaseMs;
            return "ok " + state();
        }

        if (cmd == "touch") {
            if (holderExists(c.fd)) return "err busy";
            if (!mode_.touch()) return "err cannot enable touch mode";
            c.lease = true;
            c.deadline = nowMs() + cfg_.leaseMs;
            logf("touch mode (lease: %d ms)", cfg_.leaseMs);
            return "ok " + state();
        }

        if (cmd == "pointer" || cmd == "reset") {
            revertAll();
            if (cmd == "reset" && wmi_ && !hw::isOn(*wmi_)) hw::setPower(*wmi_, true);
            return "ok " + state();
        }

        if (cmd == "power") {
            in >> a;
            if (a != "on" && a != "off") return "err usage: power on|off";
            if (!wmi_) { if (!cfg_.fake) return "err hardware unavailable"; fakePower_ = (a == "on"); return "ok " + state(); }
            return hw::setPower(*wmi_, a == "on") ? "ok " + state() : "err write refused";
        }

        if (cmd == "brightness") {
            int v = 0;
            in >> v;
            if (!wmi_) {
                if (!cfg_.fake) return "err hardware unavailable";
                if (v < 1 || v > 255) return "err brightness: integer from 1 to 255";
                fakeBrightness_ = v;
                return "ok " + state();
            }
            if (!hw::setBrightness(*wmi_, v)) return "err brightness: integer from 1 to 255";
            cfg_.brightness = v;
            saveSettings(cfg_);
            return "ok " + state();
        }

        if (cmd == "lastmode") {
            if (in >> a) {
                if (a != "trackpad" && a != "screenpad" && a != "off") return "err lastmode: trackpad, screenpad or off";
                cfg_.lastMode = a;
                saveSettings(cfg_);
                applyPanelPower();
                refreshReader();
            }
            return "ok " + state();
        }

        if (cmd == "watch") {
            in >> a;
            c.watch = (a == "on");
            refreshReader();
            return c.watch && !cfg_.fake && !reader_.running() ? "err pad not found" : "ok";
        }

        if (cmd == "trackpad") {
            in >> a;
            if (a == "off") { if (trackpadOwner_ == c.fd) trackpadOff(false); return "ok"; }
            if (a != "on") return "err usage: trackpad on x0 y0 x1 y1 | off";
            double x0, y0, x1, y1;
            if (!(in >> x0 >> y0 >> x1 >> y1)) return "err usage: trackpad on x0 y0 x1 y1";
            if (!c.lease || !mode_.isTouch()) return "err touch mode required";
            trackpadOn(c.fd, x0, y0, x1, y1);
            if (!cfg_.fake && !(vptr_ && vptr_->ok() && reader_.running() && reader_.grabbed())) { trackpadOff(false); return "err trackpad unavailable (uinput or pad not found)"; }
            return "ok";
        }

        return "err unknown command";
    }

    void trackpadOn(int owner, double x0, double y0, double x1, double y1) {
        trackpadOff(false);
        trackpadOwner_ = owner;
        closeFilter_ = std::make_unique<CloseFilter>([this] { sendFd(trackpadOwner_, "event close"); });
        closeFilter_->setRect(x0, y0, x1, y1);
        refreshReader();
        if (cfg_.fake || !reader_.running()) return;
        const PadGeometry g = reader_.geometry();
        logf("virtual touchpad %dx%d units, %d and %d units per mm", g.width, g.height, g.resX, g.resY);
        vptr_ = std::make_unique<VirtualTouchpad>(g);
    }

    void trackpadOff(bool notify) {
        const int owner = trackpadOwner_;
        trackpadOwner_ = -1;
        closeFilter_.reset();
        vptr_.reset();
        refreshReader();
        if (notify && owner >= 0) sendFd(owner, "event close");
    }

    void refreshReader() {
        if (cfg_.fake) return;
        const bool grab = trackpadOwner_ >= 0 || cfg_.lastMode == "off";
        bool watch = false;
        for (auto& [fd, c] : clients_) watch = watch || c.watch;
        if (!grab && !watch) { reader_.stop(); return; }
        if (!reader_.running() || reader_.grabbed() != grab) {
            if (!reader_.start(grab)) logf("cannot read the pad");
        }
    }

    void onPhysicalButton(bool down) {
        if (vptr_) vptr_->button(down);
    }

    void onFrame(const Frame& f) {
        if (closeFilter_) {
            const Frame forwarded = closeFilter_->feed(f, nowMs());
            if (vptr_) vptr_->feed(forwarded);
        }
        std::string msg;
        for (auto& [fd, c] : clients_) {
            if (!c.watch) continue;
            if (msg.empty()) {
                std::ostringstream o;
                o << "event raw";
                int n = 0;
                for (int i = 0; i < 10; ++i) if (f[i].down) { ++n; char b[48]; std::snprintf(b, sizeof b, " %d:%.3f:%.3f", i, f[i].x, f[i].y); o << b; }
                if (n == 0) o << " none";
                msg = o.str();
            }
            send(c, msg);
        }
    }

    void revertAll() {
        trackpadOff(false);
        mode_.pointer();
        for (auto& [fd, c] : clients_) c.lease = false;
        refreshReader();
    }

    static double suspendedSeconds() {
        timespec b{}, m{};
        ::clock_gettime(CLOCK_BOOTTIME, &b);
        ::clock_gettime(CLOCK_MONOTONIC, &m);
        return static_cast<double>(b.tv_sec - m.tv_sec) + static_cast<double>(b.tv_nsec - m.tv_nsec) / 1e9;
    }

    void applyPanelPower() {
        const bool want = cfg_.lastMode == "screenpad";
        if (wmi_) {
            if (hw::isOn(*wmi_) != want) hw::setPower(*wmi_, want);
            if (want && cfg_.brightness > 0) hw::setBrightness(*wmi_, cfg_.brightness);
        } else if (cfg_.fake) {
            fakePower_ = want;
        }
    }

    void maintainLauncher() {
        if (cfg_.fake) return;
        if (launcherPid_ > 0 && ::waitpid(launcherPid_, nullptr, WNOHANG) != 0) {
            launcherPid_ = 0;
            nextSpawnMs_ = nowMs() + 3000;
        }
        if (launcherPid_ > 0 || cfg_.lastMode != "screenpad" || nowMs() < nextSpawnMs_) return;
        for (auto& [fd, c] : clients_) if (c.lease) return;
        const auto s = session::find();
        if (!s) return;
        logf("starting the launcher for %s", s->user.c_str());
        launcherPid_ = session::spawnLauncher(*s, cfg_.launcherScale);
    }

    void checkResume() {
        const double now = suspendedSeconds();
        if (now - lastSuspended_ > 1.0) {
            logf("resume from suspend: back to pointer mode, panel powered on");
            revertAll();
            applyPanelPower();
        }
        lastSuspended_ = now;
    }

    void checkLeases() {
        const int64_t now = nowMs();
        for (auto& [fd, c] : clients_)
            if (c.lease && now > c.deadline) { logf("lease expired: back to pointer mode"); drop_.push_back(fd); }
    }

    void flushDrops() {
        for (int fd : drop_) {
            auto it = clients_.find(fd);
            if (it == clients_.end()) continue;
            const bool hadLease = it->second.lease;
            const bool owned = trackpadOwner_ == fd;
            ::close(fd);
            clients_.erase(it);
            if (owned) trackpadOff(false);
            if (hadLease) { logf("lease holder gone: back to pointer mode"); revertAll(); }
            else refreshReader();
        }
        drop_.clear();
    }

    Config cfg_;
    Mode mode_;
    TouchReader reader_;
    pid_t launcherPid_ = 0;
    int64_t nextSpawnMs_ = 0;
    std::unique_ptr<hw::Wmi> wmi_;
    std::unique_ptr<VirtualTouchpad> vptr_;
    std::unique_ptr<CloseFilter> closeFilter_;
    std::map<int, Client> clients_;
    std::vector<int> drop_;
    int listenFd_ = -1;
    int trackpadOwner_ = -1;
    double lastSuspended_ = suspendedSeconds();
    bool fakePower_ = true;
    int fakeBrightness_ = 128;
};

}

int main(int argc, char** argv) {
    Config cfg = loadConfig();
    bool revertOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--fake") cfg.fake = true;
        else if (a == "--revert") revertOnly = true;
        else if (a == "--lease-ms" && i + 1 < argc) cfg.leaseMs = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "usage: screenpadctld [--fake] [--lease-ms N] [--revert]\n"); return 2; }
    }
    if (revertOnly) { Mode m(cfg); m.pointer(); return 0; }
    if (!cfg.fake && ::geteuid() != 0) { std::fputs("screenpadctld: root required (or --fake to test)\n", stderr); return 1; }
    return Daemon(std::move(cfg)).run();
}
