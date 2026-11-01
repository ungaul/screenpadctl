#pragma once

#include <cstdlib>
#include <dirent.h>
#include <grp.h>
#include <optional>
#include <pwd.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct Session {
    uid_t uid = 0;
    gid_t gid = 0;
    std::string user, home, runtime, signature, wayland, display, xauthority;
};

namespace session {

inline std::string firstEntry(const std::string& dir, bool (*accept)(const std::string&)) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return {};
    std::string found;
    while (dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name != "." && name != ".." && accept(name)) { found = name; break; }
    }
    ::closedir(d);
    return found;
}

inline std::string xDisplay(uid_t uid) {
    DIR* d = ::opendir("/tmp/.X11-unix");
    if (!d) return {};
    std::string found;
    while (dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        struct stat st {};
        if (name.size() < 2 || name[0] != 'X' || ::stat(("/tmp/.X11-unix/" + name).c_str(), &st) != 0 || st.st_uid != uid) continue;
        found = ":" + name.substr(1);
        break;
    }
    ::closedir(d);
    return found;
}

inline std::optional<Session> find() {
    DIR* d = ::opendir("/run/user");
    if (!d) return std::nullopt;
    std::optional<Session> result;
    while (dirent* e = ::readdir(d)) {
        const uid_t uid = static_cast<uid_t>(std::strtoul(e->d_name, nullptr, 10));
        if (uid < 1000) continue;
        const std::string runtime = std::string("/run/user/") + e->d_name;
        const std::string signature = firstEntry(runtime + "/hypr", [](const std::string& n) { return n.find('_') != std::string::npos; });
        const std::string wayland = firstEntry(runtime, [](const std::string& n) { return n.rfind("wayland-", 0) == 0 && n.find(".lock") == std::string::npos; });
        const passwd* pw = ::getpwuid(uid);
        if (!pw) continue;
        const bool hypr = !signature.empty() && !wayland.empty();
        const std::string display = hypr ? std::string() : xDisplay(uid);
        if (!hypr && display.empty()) continue;
        result = Session{uid, pw->pw_gid, pw->pw_name, pw->pw_dir, runtime, signature, wayland, display, std::string(pw->pw_dir) + "/.Xauthority"};
        break;
    }
    ::closedir(d);
    return result;
}

inline pid_t spawnLauncher(const Session& s, const std::string& scale) {
    const pid_t pid = ::fork();
    if (pid != 0) return pid;
    ::setsid();
    if (::initgroups(s.user.c_str(), s.gid) != 0 || ::setgid(s.gid) != 0 || ::setuid(s.uid) != 0) ::_exit(126);
    if (::chdir(s.home.c_str()) != 0) ::_exit(126);
    ::clearenv();
    ::setenv("HOME", s.home.c_str(), 1);
    ::setenv("USER", s.user.c_str(), 1);
    ::setenv("PATH", "/usr/local/bin:/usr/bin", 1);
    ::setenv("XDG_RUNTIME_DIR", s.runtime.c_str(), 1);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + s.runtime + "/bus").c_str(), 1);
    if (s.display.empty()) {
        ::setenv("WAYLAND_DISPLAY", s.wayland.c_str(), 1);
        ::setenv("HYPRLAND_INSTANCE_SIGNATURE", s.signature.c_str(), 1);
        ::setenv("QT_QPA_PLATFORM", "wayland", 1);
    } else {
        ::setenv("DISPLAY", s.display.c_str(), 1);
        ::setenv("XAUTHORITY", s.xauthority.c_str(), 1);
        ::setenv("QT_QPA_PLATFORM", "xcb", 1);
        if (!scale.empty()) ::setenv("QT_SCALE_FACTOR", scale.c_str(), 1);
    }
    ::execlp("screenpadctl-launcher", "screenpadctl-launcher", static_cast<char*>(nullptr));
    ::_exit(127);
}

}
