cmake_minimum_required(VERSION 3.20)
project(screenpadctl VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)
enable_testing()

add_library(screenpadctl-hw STATIC src/common/hw.cpp)
target_compile_options(screenpadctl-hw PRIVATE -Wall -Wextra -Wpedantic)

add_executable(screenpadctl src/ctl/main.cpp)
target_link_libraries(screenpadctl PRIVATE screenpadctl-hw)
target_compile_options(screenpadctl PRIVATE -Wall -Wextra -Wpedantic)

add_executable(screenpadctld src/daemon/main.cpp)
target_link_libraries(screenpadctld PRIVATE screenpadctl-hw)
target_compile_options(screenpadctld PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS screenpadctl screenpadctld RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set(SYSTEMD_UNIT_DIR "lib/systemd/system" CACHE STRING "systemd unit directory (relative to the prefix)")
configure_file(data/screenpadctld.service.in screenpadctld.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/screenpadctld.service DESTINATION ${SYSTEMD_UNIT_DIR})
install(FILES data/screenpadctl.conf.example DESTINATION share/doc/screenpadctl)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gesture.cpp)
    add_executable(test-gesture tests/test_gesture.cpp)
    target_include_directories(test-gesture PRIVATE src/daemon)
    add_test(NAME gesture COMMAND test-gesture)
endif()

option(BUILD_LAUNCHER "Build the Qt6 interface" ON)
if(BUILD_LAUNCHER)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Network)
    qt_standard_project_setup(REQUIRES 6.5)
    qt_policy(SET QTP0004 NEW)

    qt_add_executable(screenpadctl-launcher
        src/launcher/main.cpp
        src/launcher/desktopmodel.cpp
        src/launcher/hyprland.cpp
        src/launcher/daemonlink.cpp
        src/launcher/palette.cpp
        src/launcher/uisettings.cpp)
    qt_add_qml_module(screenpadctl-launcher
        URI Screenpadctl VERSION 1.0
        QML_FILES qml/Main.qml qml/SettingSlider.qml qml/SettingToggle.qml)
    target_link_libraries(screenpadctl-launcher PRIVATE Qt6::Quick Qt6::Network)
    target_compile_options(screenpadctl-launcher PRIVATE -Wall -Wextra)
    install(TARGETS screenpadctl-launcher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

set(CPACK_PACKAGE_NAME screenpadctl)
set(CPACK_PACKAGE_CONTACT "ungaul")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ScreenXpert-like interface for the ASUS ScreenPad")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qml6-module-qtquick, qml6-module-qtquick-window")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/usr/lib/systemd;/usr/lib/systemd/system;/usr/share/doc")
include(CPack)
