#pragma once

#include <cstdlib>

namespace proto {
inline const char* socketPath() {
    const char* p = std::getenv("SCREENPADCTL_SOCKET");
    return p && *p ? p : "/run/screenpadctl.sock";
}
}
