#include "../src/daemon/closefilter.h"

#include <cstdio>
#include <initializer_list>
#include <utility>

namespace {
int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

Frame fingers(std::initializer_list<std::pair<double, double>> pts) {
    Frame f{};
    size_t i = 0;
    for (auto [x, y] : pts) f[i++] = {true, x, y};
    return f;
}

int countDown(const Frame& f) {
    int n = 0;
    for (const auto& s : f) n += s.down ? 1 : 0;
    return n;
}
}

int main() {
    {
        int closes = 0;
        CloseFilter filter([&] { ++closes; });
        filter.setRect(0.9, 0.0, 1.0, 0.2);
        const Frame down = filter.feed(fingers({{0.95, 0.1}}), 0);
        CHECK(countDown(down) == 0);
        filter.feed({}, 80);
        CHECK(closes == 1);
    }

    {
        int closes = 0;
        CloseFilter filter([&] { ++closes; });
        filter.setRect(0.9, 0.0, 1.0, 0.2);
        const Frame down = filter.feed(fingers({{0.5, 0.5}}), 0);
        CHECK(countDown(down) == 1);
        filter.feed({}, 80);
        CHECK(closes == 0);
    }

    {
        int closes = 0;
        CloseFilter filter([&] { ++closes; });
        filter.setRect(0.9, 0.0, 1.0, 0.2);
        filter.feed(fingers({{0.95, 0.1}}), 0);
        filter.feed({}, 800);
        CHECK(closes == 0);
    }

    {
        int closes = 0;
        CloseFilter filter([&] { ++closes; });
        filter.setRect(0.9, 0.0, 1.0, 0.2);
        filter.feed(fingers({{0.5, 0.5}}), 0);
        const Frame slid = filter.feed(fingers({{0.95, 0.1}}), 16);
        CHECK(countDown(slid) == 1);
        filter.feed({}, 40);
        CHECK(closes == 0);
    }

    {
        int closes = 0;
        CloseFilter filter([&] { ++closes; });
        filter.setRect(0.9, 0.0, 1.0, 0.2);
        const Frame two = filter.feed(fingers({{0.95, 0.1}, {0.4, 0.5}}), 0);
        CHECK(countDown(two) == 1);
    }

    if (failures) return 1;
    std::puts("close filter tests passed");
    return 0;
}
