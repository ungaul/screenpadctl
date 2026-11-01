#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

struct Finger {
    bool down = false;
    double x = 0, y = 0;
};
using Frame = std::array<Finger, 10>;

class CloseFilter {
public:
    explicit CloseFilter(std::function<void()> onClose) : onClose_(std::move(onClose)) {}

    void setRect(double x0, double y0, double x1, double y1) { x0_ = x0; y0_ = y0; x1_ = x1; y1_ = y1; }

    Frame feed(const Frame& in, int64_t nowMs) {
        Frame out = in;
        for (size_t i = 0; i < in.size(); ++i) {
            Track& t = tracks_[i];
            if (in[i].down && !prev_[i].down) {
                t = {};
                t.swallowed = inside(in[i].x, in[i].y);
                t.t0 = nowMs;
                t.x0 = in[i].x;
                t.y0 = in[i].y;
            }
            if (in[i].down && t.swallowed) {
                t.travel = std::fmax(t.travel, std::hypot(in[i].x - t.x0, in[i].y - t.y0));
                out[i].down = false;
            }
            if (!in[i].down && prev_[i].down && t.swallowed && nowMs - t.t0 < 300 && t.travel < 0.03 && onClose_) onClose_();
            if (!in[i].down) t.swallowed = false;
        }
        prev_ = in;
        return out;
    }

private:
    struct Track {
        bool swallowed = false;
        int64_t t0 = 0;
        double x0 = 0, y0 = 0, travel = 0;
    };

    bool inside(double x, double y) const { return x >= x0_ && x <= x1_ && y >= y0_ && y <= y1_; }

    std::function<void()> onClose_;
    double x0_ = 2, y0_ = 2, x1_ = -1, y1_ = -1;
    std::array<Track, 10> tracks_{};
    Frame prev_{};
};
