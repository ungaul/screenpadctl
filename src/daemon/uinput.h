#pragma once

#include <array>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "closefilter.h"

struct PadGeometry {
    int width = 0, height = 0, resX = 0, resY = 0;
};

class VirtualTouchpad {
public:
    explicit VirtualTouchpad(const PadGeometry& g) : geo_(g) {
        fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd_ < 0) return;
        ::ioctl(fd_, UI_SET_EVBIT, EV_KEY);
        ::ioctl(fd_, UI_SET_EVBIT, EV_ABS);
        for (int b : {BTN_LEFT, BTN_TOUCH, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP, BTN_TOOL_QUADTAP, BTN_TOOL_QUINTTAP})
            ::ioctl(fd_, UI_SET_KEYBIT, b);
        ::ioctl(fd_, UI_SET_PROPBIT, INPUT_PROP_POINTER);
        ::ioctl(fd_, UI_SET_PROPBIT, INPUT_PROP_BUTTONPAD);
        setAxis(ABS_X, 0, g.width - 1, g.resX);
        setAxis(ABS_Y, 0, g.height - 1, g.resY);
        setAxis(ABS_MT_POSITION_X, 0, g.width - 1, g.resX);
        setAxis(ABS_MT_POSITION_Y, 0, g.height - 1, g.resY);
        setAxis(ABS_MT_SLOT, 0, kSlots - 1, 0);
        setAxis(ABS_MT_TRACKING_ID, 0, 65535, 0);
        uinput_setup us{};
        us.id = {BUS_USB, 0x1d6b, 0x0105, 1};
        std::strncpy(us.name, "screenpadctl virtual touchpad", UINPUT_MAX_NAME_SIZE - 1);
        if (::ioctl(fd_, UI_DEV_SETUP, &us) < 0 || ::ioctl(fd_, UI_DEV_CREATE) < 0) { ::close(fd_); fd_ = -1; }
    }
    ~VirtualTouchpad() { if (fd_ >= 0) { ::ioctl(fd_, UI_DEV_DESTROY); ::close(fd_); } }
    VirtualTouchpad(const VirtualTouchpad&) = delete;
    VirtualTouchpad& operator=(const VirtualTouchpad&) = delete;

    bool ok() const { return fd_ >= 0; }

    void feed(const Frame& f) {
        bool changed = false;
        int count = 0, first = -1;
        for (int i = 0; i < kSlots; ++i) {
            const Finger& now = f[static_cast<size_t>(i)];
            Finger& was = prev_[static_cast<size_t>(i)];
            if (now.down) { ++count; if (first < 0) first = i; }
            if (now.down) {
                const int x = static_cast<int>(now.x * (geo_.width - 1)), y = static_cast<int>(now.y * (geo_.height - 1));
                if (!was.down) {
                    emit(EV_ABS, ABS_MT_SLOT, i);
                    emit(EV_ABS, ABS_MT_TRACKING_ID, nextId_++ & 0xffff);
                    emit(EV_ABS, ABS_MT_POSITION_X, x);
                    emit(EV_ABS, ABS_MT_POSITION_Y, y);
                    changed = true;
                } else if (x != lastX_[static_cast<size_t>(i)] || y != lastY_[static_cast<size_t>(i)]) {
                    emit(EV_ABS, ABS_MT_SLOT, i);
                    emit(EV_ABS, ABS_MT_POSITION_X, x);
                    emit(EV_ABS, ABS_MT_POSITION_Y, y);
                    changed = true;
                }
                lastX_[static_cast<size_t>(i)] = x;
                lastY_[static_cast<size_t>(i)] = y;
            } else if (was.down) {
                emit(EV_ABS, ABS_MT_SLOT, i);
                emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
                changed = true;
            }
            was = now;
        }
        if (!changed) return;
        if (first >= 0) {
            emit(EV_ABS, ABS_X, lastX_[static_cast<size_t>(first)]);
            emit(EV_ABS, ABS_Y, lastY_[static_cast<size_t>(first)]);
        }
        setTools(count);
        emit(EV_SYN, SYN_REPORT, 0);
    }

    void button(bool down) {
        emit(EV_KEY, BTN_LEFT, down ? 1 : 0);
        emit(EV_SYN, SYN_REPORT, 0);
    }

private:
    static constexpr int kSlots = 5;

    void setAxis(int code, int min, int max, int res) {
        ::ioctl(fd_, UI_SET_ABSBIT, code);
        uinput_abs_setup a{};
        a.code = static_cast<__u16>(code);
        a.absinfo.minimum = min;
        a.absinfo.maximum = max;
        a.absinfo.resolution = res;
        ::ioctl(fd_, UI_ABS_SETUP, &a);
    }

    void setTools(int count) {
        if (count == tools_) return;
        const int codes[] = {BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP, BTN_TOOL_QUADTAP, BTN_TOOL_QUINTTAP};
        if (tools_ > 0) emit(EV_KEY, codes[tools_ - 1], 0);
        if (count > 0) emit(EV_KEY, codes[count - 1], 1);
        if ((tools_ > 0) != (count > 0)) emit(EV_KEY, BTN_TOUCH, count > 0 ? 1 : 0);
        tools_ = count;
    }

    void emit(int type, int code, int value) {
        if (fd_ < 0) return;
        input_event ev{};
        ev.type = static_cast<__u16>(type);
        ev.code = static_cast<__u16>(code);
        ev.value = value;
        [[maybe_unused]] const ssize_t written = ::write(fd_, &ev, sizeof ev);
    }

    int fd_ = -1;
    PadGeometry geo_;
    Frame prev_{};
    std::array<int, 10> lastX_{}, lastY_{};
    int tools_ = 0;
    int nextId_ = 1;
};
