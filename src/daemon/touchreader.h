#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include "uinput.h"

class TouchReader {
public:
    struct Cfg {
        std::string namePrefix;
        int rotation = 270;
    };
    using FrameCb = std::function<void(const Frame&)>;
    using ButtonCb = std::function<void(bool)>;

    TouchReader(Cfg cfg, FrameCb cb, ButtonCb button) : cfg_(std::move(cfg)), cb_(std::move(cb)), button_(std::move(button)) {}
    ~TouchReader() { stop(); }

    bool start(bool grab) {
        stop();
        DIR* d = ::opendir("/dev/input");
        if (!d) return false;
        while (dirent* e = ::readdir(d)) {
            if (std::strncmp(e->d_name, "event", 5) != 0) continue;
            const std::string path = std::string("/dev/input/") + e->d_name;
            int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            char name[256] = {};
            if (::ioctl(fd, EVIOCGNAME(sizeof name), name) < 0 || std::strncmp(name, cfg_.namePrefix.c_str(), cfg_.namePrefix.size()) != 0) {
                ::close(fd);
                continue;
            }
            Node n;
            n.fd = fd;
            n.name = name;
            input_absinfo ax{}, ay{};
            n.mt = ::ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax) == 0 && ::ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay) == 0;
            if (n.mt) { n.minX = ax.minimum; n.maxX = ax.maximum; n.minY = ay.minimum; n.maxY = ay.maximum; n.resX = ax.resolution; n.resY = ay.resolution; }
            if (grab && ::ioctl(fd, EVIOCGRAB, 1) < 0) { ::close(fd); continue; }
            nodes_.push_back(std::move(n));
        }
        ::closedir(d);

        primary_ = -1;
        for (size_t i = 0; i < nodes_.size(); ++i)
            if (nodes_[i].mt && nodes_[i].name.size() >= 8 && nodes_[i].name.compare(nodes_[i].name.size() - 8, 8, "Touchpad") == 0) { primary_ = static_cast<int>(i); break; }
        if (primary_ < 0)
            for (size_t i = 0; i < nodes_.size(); ++i) if (nodes_[i].mt) { primary_ = static_cast<int>(i); break; }
        if (primary_ < 0) { stop(); return false; }
        grabbed_ = grab;
        return true;
    }

    void stop() {
        for (auto& n : nodes_) {
            if (grabbed_) ::ioctl(n.fd, EVIOCGRAB, 0);
            ::close(n.fd);
        }
        nodes_.clear();
        primary_ = -1;
        grabbed_ = false;
    }

    PadGeometry geometry() const {
        if (primary_ < 0) return {};
        const Node& n = nodes_[static_cast<size_t>(primary_)];
        const int w = n.maxX - n.minX + 1, h = n.maxY - n.minY + 1;
        if (h > w && cfg_.rotation % 180 != 0) return {h, w, n.resY, n.resX};
        return {w, h, n.resX, n.resY};
    }

    bool running() const { return !nodes_.empty(); }
    bool grabbed() const { return grabbed_; }
    bool lost() const { return lost_; }
    std::vector<int> fds() const { std::vector<int> v; for (auto& n : nodes_) v.push_back(n.fd); return v; }

    void onReadable(int fd) {
        for (size_t i = 0; i < nodes_.size(); ++i) if (nodes_[i].fd == fd) { drain(nodes_[i], static_cast<int>(i) == primary_); return; }
    }

private:
    struct Node {
        int fd = -1;
        std::string name;
        bool mt = false;
        int minX = 0, maxX = 1, minY = 0, maxY = 1, resX = 0, resY = 0;
        int slot = 0;
        struct Slot { bool down = false; int x = 0, y = 0; } slots[10];
    };

    void drain(Node& n, bool primary) {
        input_event ev[64];
        for (;;) {
            ssize_t r = ::read(n.fd, ev, sizeof ev);
            if (r < 0) { if (errno != EAGAIN) lost_ = true; return; }
            if (r == 0) return;
            for (size_t i = 0; i < static_cast<size_t>(r) / sizeof(input_event); ++i) {
                const input_event& e = ev[i];
                if (e.type == EV_KEY && e.code == BTN_LEFT && e.value != 2) { if (button_) button_(e.value == 1); continue; }
                if (!primary) continue;
                if (e.type == EV_ABS) {
                    if (e.code == ABS_MT_SLOT) n.slot = (e.value >= 0 && e.value < 10) ? e.value : 0;
                    else if (e.code == ABS_MT_TRACKING_ID) n.slots[n.slot].down = e.value >= 0;
                    else if (e.code == ABS_MT_POSITION_X) n.slots[n.slot].x = e.value;
                    else if (e.code == ABS_MT_POSITION_Y) n.slots[n.slot].y = e.value;
                } else if (e.type == EV_SYN && e.code == SYN_REPORT) {
                    emitFrame(n);
                }
            }
        }
    }

    void emitFrame(const Node& n) {
        const double w = std::max(1, n.maxX - n.minX), h = std::max(1, n.maxY - n.minY);

        const int rot = (n.maxX - n.minX) >= (n.maxY - n.minY) ? 0 : cfg_.rotation;
        Frame f{};
        for (int i = 0; i < 10; ++i) {
            if (!n.slots[i].down) continue;
            const double u = (n.slots[i].x - n.minX) / w, v = (n.slots[i].y - n.minY) / h;
            double lx, ly;
            switch (rot) {
                case 90:  lx = 1 - v; ly = u; break;
                case 180: lx = 1 - u; ly = 1 - v; break;
                case 270: lx = v; ly = 1 - u; break;
                default:  lx = u; ly = v; break;
            }
            f[i] = {true, lx, ly};
        }
        if (cb_) cb_(f);
    }

    Cfg cfg_;
    FrameCb cb_;
    ButtonCb button_;
    std::vector<Node> nodes_;
    int primary_ = -1;
    bool grabbed_ = false, lost_ = false;
};
