import QtQuick

Window {
    id: root
    visible: false
    title: "screenpadctl"
    color: root.c("background")

    property string overlay: ""
    property string previousOverlay: ""
    property date now: new Date()
    readonly property color shadowColor: Qt.rgba(0, 0, 0, 0.45)

    function c(role, alpha = 1) {
        const base = Qt.color((ui.followShellColors ? theme.colors : theme.defaults)[role])
        return Qt.rgba(base.r, base.g, base.b, alpha)
    }

    function openTrackpad() {
        const p = closeBtn.mapToItem(null, 0, 0)
        const m = 10
        daemon.trackpad(true, (p.x - m) / root.width, (p.y - m) / root.height,
                              (p.x + closeBtn.width + m) / root.width, (p.y + closeBtn.height + m) / root.height)
    }

    onOverlayChanged: {
        if (overlay === "trackpad")
            openTrackpad()
        else if (previousOverlay === "trackpad")
            daemon.trackpad(false)
        previousOverlay = overlay
    }

    Connections {
        target: daemon
        function onCloseRequested() { root.overlay = "" }
        function onError(message) { root.overlay = "" }
    }

    Image {
        anchors.fill: parent
        visible: ui.showWallpaper && theme.wallpaper !== ""
        source: ui.showWallpaper ? theme.wallpaper : ""
        sourceSize.width: Math.round(root.width * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
    }
    Rectangle {
        anchors.fill: parent
        visible: ui.showWallpaper && theme.wallpaper !== ""
        color: "#000000"
        opacity: ui.wallpaperDim
    }

    Column {
        id: titleBlock
        anchors { left: parent.left; top: parent.top; leftMargin: 52; topMargin: 40 }
        Text {
            id: clock
            color: root.c("on_surface")
            font { pixelSize: ui.clockSize; weight: Font.Light }
            text: Qt.formatTime(root.now, ui.clock24h ? "hh:mm" : "h:mm AP")
            style: Text.Raised
            styleColor: root.shadowColor
        }
        Text {
            id: date
            visible: ui.showDate
            color: root.c("on_surface_variant")
            font.pixelSize: 15
            text: Qt.formatDate(root.now, "dddd d MMMM")
            style: Text.Raised
            styleColor: root.shadowColor
        }
    }
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: root.now = new Date()
    }

    GridView {
        id: grid
        anchors { left: parent.left; right: parent.right; top: titleBlock.bottom; bottom: parent.bottom
                  leftMargin: 20; rightMargin: 20; topMargin: 8; bottomMargin: 112 }
        cellWidth: 130; cellHeight: 120
        clip: true
        model: shortcuts
        boundsBehavior: Flickable.StopAtBounds
        interactive: root.overlay === ""

        delegate: Item {
            width: grid.cellWidth; height: grid.cellHeight
            Rectangle {
                anchors { fill: parent; margins: 6 }
                radius: ui.cardRadius
                color: tap.pressed ? root.c("surface_container_high", Math.min(1, ui.cardOpacity + 0.13)) : root.c("surface_container", ui.cardOpacity)
                Behavior on color { ColorAnimation { duration: 80 } }

                Image {
                    anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 12 }
                    width: ui.iconSize; height: ui.iconSize
                    sourceSize: Qt.size(ui.iconSize * 2, ui.iconSize * 2)
                    fillMode: Image.PreserveAspectCrop
                    source: model.icon
                    asynchronous: true
                }
                Text {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                              leftMargin: 6; rightMargin: 6; bottomMargin: 10 }
                    text: model.name
                    color: root.c("on_surface"); font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
                TapHandler { id: tap; enabled: root.overlay === ""; onTapped: shortcuts.launch(index) }
            }
        }

        Text {
            visible: grid.count === 0
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            color: root.c("on_surface_variant"); font.pixelSize: 16
            text: "No shortcuts.\nAdd .desktop launchers or folders to ~/Desktop"
        }
    }

    Rectangle {
        id: padButton
        width: 60; height: 60; radius: 16
        anchors { left: parent.left; bottom: parent.bottom; leftMargin: 44; bottomMargin: 36 }
        color: padTap.pressed ? Qt.lighter(root.c("primary"), 1.15) : root.c("primary")
        opacity: daemon.ready ? 1 : 0.35

        Rectangle {
            width: 28; height: 22; radius: 5
            color: "transparent"
            border { color: root.c("on_primary"); width: 2 }
            anchors { horizontalCenter: parent.horizontalCenter; verticalCenter: parent.verticalCenter; verticalCenterOffset: -2 }
        }
        Rectangle {
            width: 2; height: 6
            color: root.c("on_primary")
            anchors { horizontalCenter: parent.horizontalCenter; verticalCenter: parent.verticalCenter; verticalCenterOffset: 8 }
        }
        TapHandler { id: padTap; enabled: daemon.ready && root.overlay === ""; onTapped: root.overlay = "trackpad" }
    }

    Rectangle {
        id: settingsButton
        width: 60; height: 60; radius: 16
        anchors { right: parent.right; bottom: parent.bottom; rightMargin: 44; bottomMargin: 36 }
        color: settingsTap.pressed ? Qt.lighter(root.c("primary"), 1.15) : root.c("primary")

        Item {
            width: 28; height: 22
            anchors.centerIn: parent
            Repeater {
                model: [{ y: 0, x: 16 }, { y: 7, x: 6 }, { y: 14, x: 12 }]
                Item {
                    width: 28; height: 8
                    y: modelData.y
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28; height: 2; radius: 1
                        color: root.c("on_primary")
                    }
                    Rectangle {
                        x: modelData.x
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4
                        color: root.c("on_primary")
                    }
                }
            }
        }
        TapHandler { id: settingsTap; enabled: root.overlay === ""; onTapped: root.overlay = "settings" }
    }

    Item {
        id: trackpadOverlay
        anchors.fill: parent
        visible: opacity > 0
        opacity: root.overlay === "trackpad" ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }

        Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45 }

        Rectangle {
            anchors { fill: parent; margins: 10 }
            radius: 26
            color: root.c("surface_container", 0.62)
            border { color: root.c("primary"); width: 2 }

            Rectangle {
                id: closeBtn
                width: 64; height: 64; radius: 18
                anchors { right: parent.right; top: parent.top; margins: 12 }
                color: root.c("primary_container")
                Rectangle {
                    width: 28; height: 3; radius: 2; rotation: 45
                    color: root.c("on_primary_container")
                    anchors.centerIn: parent
                }
                Rectangle {
                    width: 28; height: 3; radius: 2; rotation: -45
                    color: root.c("on_primary_container")
                    anchors.centerIn: parent
                }
                TapHandler { onTapped: root.overlay = "" }
            }
        }
    }

    Item {
        id: settingsOverlay
        anchors.fill: parent
        visible: opacity > 0
        opacity: root.overlay === "settings" ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }

        Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45 }

        Rectangle {
            anchors { fill: parent; margins: 20 }
            radius: 26
            color: root.c("surface_container", 0.88)
            border { color: root.c("primary"); width: 2 }

            Text {
                id: settingsTitle
                anchors { left: parent.left; top: parent.top; leftMargin: 28; topMargin: 22 }
                text: "Settings"
                color: root.c("on_surface")
                font.pixelSize: 22
            }

            Rectangle {
                id: settingsClose
                width: 64; height: 64; radius: 18
                anchors { right: parent.right; top: parent.top; margins: 12 }
                color: root.c("primary_container")
                Rectangle {
                    width: 28; height: 3; radius: 2; rotation: 45
                    color: root.c("on_primary_container")
                    anchors.centerIn: parent
                }
                Rectangle {
                    width: 28; height: 3; radius: 2; rotation: -45
                    color: root.c("on_primary_container")
                    anchors.centerIn: parent
                }
                TapHandler { onTapped: root.overlay = "" }
            }

            Flickable {
                id: settingsFlick
                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: parent.bottom
                          topMargin: 92; bottomMargin: 16; leftMargin: 28; rightMargin: 28 }
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                contentHeight: settingsColumn.height

                Column {
                    id: settingsColumn
                    width: settingsFlick.width
                    spacing: 8

                    Text {
                        width: parent.width; height: 36
                        verticalAlignment: Text.AlignBottom
                        text: "DEVICE"
                        color: root.c("primary")
                        font { pixelSize: 14; weight: Font.DemiBold; letterSpacing: 1.5 }
                    }

                    SettingSlider {
                        id: brightnessSlider
                        width: parent.width; height: 64
                        label: "Brightness"
                        from: 1; to: 255
                        value: daemon.brightness
                        valueText: Math.round(brightnessSlider.shownValue / 255 * 100) + "%"
                        textColor: root.c("on_surface")
                        dimColor: root.c("on_surface_variant")
                        trackColor: root.c("outline_variant")
                        fillColor: root.c("primary")
                        knobColor: root.c("on_surface")
                        onReleased: (v) => daemon.setBrightness(Math.round(v))
                    }

                    Text {
                        width: parent.width; height: 36
                        verticalAlignment: Text.AlignBottom
                        text: "APPEARANCE"
                        color: root.c("primary")
                        font { pixelSize: 14; weight: Font.DemiBold; letterSpacing: 1.5 }
                    }

                    SettingToggle {
                        width: parent.width; height: 64
                        label: "Show wallpaper"
                        checked: ui.showWallpaper
                        textColor: root.c("on_surface")
                        onColor: root.c("primary")
                        offColor: root.c("outline_variant")
                        knobOnColor: root.c("on_primary")
                        knobOffColor: root.c("on_surface_variant")
                        onToggled: ui.showWallpaper = !ui.showWallpaper
                    }

                    SettingSlider {
                        id: dimSlider
                        width: parent.width; height: 64
                        label: "Wallpaper dimming"
                        from: 0; to: 0.9
                        value: ui.wallpaperDim
                        valueText: Math.round(dimSlider.shownValue * 100) + "%"
                        textColor: root.c("on_surface")
                        dimColor: root.c("on_surface_variant")
                        trackColor: root.c("outline_variant")
                        fillColor: root.c("primary")
                        knobColor: root.c("on_surface")
                        onMoved: (v) => ui.wallpaperDim = v
                    }

                    SettingToggle {
                        width: parent.width; height: 64
                        label: "Custom colors"
                        checked: ui.followShellColors
                        textColor: root.c("on_surface")
                        onColor: root.c("primary")
                        offColor: root.c("outline_variant")
                        knobOnColor: root.c("on_primary")
                        knobOffColor: root.c("on_surface_variant")
                        onToggled: ui.followShellColors = !ui.followShellColors
                    }

                    SettingSlider {
                        id: cardOpacitySlider
                        width: parent.width; height: 64
                        label: "Card opacity"
                        from: 0.2; to: 1.0
                        value: ui.cardOpacity
                        valueText: Math.round(cardOpacitySlider.shownValue * 100) + "%"
                        textColor: root.c("on_surface")
                        dimColor: root.c("on_surface_variant")
                        trackColor: root.c("outline_variant")
                        fillColor: root.c("primary")
                        knobColor: root.c("on_surface")
                        onMoved: (v) => ui.cardOpacity = v
                    }

                    SettingSlider {
                        id: cardRadiusSlider
                        width: parent.width; height: 64
                        label: "Card roundness"
                        from: 0; to: 28
                        value: ui.cardRadius
                        valueText: Math.round(cardRadiusSlider.shownValue) + "px"
                        textColor: root.c("on_surface")
                        dimColor: root.c("on_surface_variant")
                        trackColor: root.c("outline_variant")
                        fillColor: root.c("primary")
                        knobColor: root.c("on_surface")
                        onMoved: (v) => ui.cardRadius = Math.round(v)
                    }

                    SettingSlider {
                        id: iconSizeSlider
                        width: parent.width; height: 64
                        label: "Icon size"
                        from: 40; to: 80
                        value: ui.iconSize
                        valueText: Math.round(iconSizeSlider.shownValue) + "px"
                        textColor: root.c("on_surface")
                        dimColor: root.c("on_surface_variant")
                        trackColor: root.c("outline_variant")
                        fillColor: root.c("primary")
                        knobColor: root.c("on_surface")
                        onMoved: (v) => ui.iconSize = Math.round(v)
                    }

                    SettingToggle {
                        width: parent.width; height: 64
                        label: "24-hour clock"
                        checked: ui.clock24h
                        textColor: root.c("on_surface")
                        onColor: root.c("primary")
                        offColor: root.c("outline_variant")
                        knobOnColor: root.c("on_primary")
                        knobOffColor: root.c("on_surface_variant")
                        onToggled: ui.clock24h = !ui.clock24h
                    }

                    SettingToggle {
                        width: parent.width; height: 64
                        label: "Show date"
                        checked: ui.showDate
                        textColor: root.c("on_surface")
                        onColor: root.c("primary")
                        offColor: root.c("outline_variant")
                        knobOnColor: root.c("on_primary")
                        knobOffColor: root.c("on_surface_variant")
                        onToggled: ui.showDate = !ui.showDate
                    }

                    SettingSlider {
                        id: clockSizeSlider
                        width: parent.width; height: 64
                        label: "Clock size"
                        from: 28; to: 64
                        value: ui.clockSize
                        valueText: Math.round(clockSizeSlider.shownValue) + "px"
                        textColor: root.c("on_surface")
                        dimColor: root.c("on_surface_variant")
                        trackColor: root.c("outline_variant")
                        fillColor: root.c("primary")
                        knobColor: root.c("on_surface")
                        onMoved: (v) => ui.clockSize = Math.round(v)
                    }
                }
            }
        }
    }
}
