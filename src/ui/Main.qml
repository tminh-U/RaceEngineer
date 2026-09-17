pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1180
    height: 760
    minimumWidth: 920
    minimumHeight: 620
    visible: true
    title: "AI Race Engineer"
    color: "#0b0d12"
    onClosing: function(close) {
        close.accepted = false
        window.hide()
    }

    readonly property color panel: "#131721"
    readonly property color panelRaised: "#191e2a"
    readonly property color border: "#272d3a"
    readonly property color textPrimary: "#f3f5f8"
    readonly property color textSecondary: "#969eae"
    readonly property color accent: "#55e09c"
    readonly property color warning: "#ffc857"
    property int currentPage: 0
    property var telemetry: app.telemetry

    function available(value) {
        return value !== undefined && value !== null
    }

    function number(value, decimals, suffix) {
        return available(value) ? Number(value).toFixed(decimals) + suffix : "—"
    }

    function integer(value, prefix) {
        return available(value) ? prefix + Number(value).toFixed(0) : "—"
    }

    function time(value) {
        if (!available(value))
            return "—"
        const minutes = Math.floor(value / 60)
        const seconds = value - minutes * 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds.toFixed(1)
    }

    component Caption: Label {
        color: window.textSecondary
        font.pixelSize: 12
        font.weight: Font.Medium
    }

    component MetricCard: Rectangle {
        id: metricCard
        property string caption: ""
        property string value: "—"
        property string detail: ""
        implicitWidth: 180
        implicitHeight: 104
        radius: 12
        color: window.panelRaised
        border.color: window.border
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 4
            Caption { text: metricCard.caption.toUpperCase(); font.letterSpacing: 0.7 }
            Label {
                text: metricCard.value
                color: window.textPrimary
                font.pixelSize: 25
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: metricCard.detail
                visible: text.length > 0
                color: window.textSecondary
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    component StatusPill: Rectangle {
        id: statusPill
        property string label: ""
        property color dotColor: window.textSecondary
        implicitWidth: pillRow.implicitWidth + 24
        implicitHeight: 30
        radius: 15
        color: "#202631"

        RowLayout {
            id: pillRow
            anchors.centerIn: parent
            spacing: 7
            Rectangle { Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 4; color: statusPill.dotColor }
            Label { text: statusPill.label; color: window.textPrimary; font.pixelSize: 12 }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 218
            Layout.fillHeight: true
            color: "#0e1118"
            border.color: "#1c212c"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 8

                RowLayout {
                    Layout.bottomMargin: 26
                    spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 10
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#55e09c" }
                            GradientStop { position: 1; color: "#32aee6" }
                        }
                        Label { anchors.centerIn: parent; text: "RE"; color: "#07110d"; font.bold: true }
                    }
                    ColumnLayout {
                        spacing: 0
                        Label { text: "RACE"; color: window.textPrimary; font.pixelSize: 15; font.bold: true; font.letterSpacing: 1.3 }
                        Label { text: "ENGINEER"; color: window.textSecondary; font.pixelSize: 10; font.letterSpacing: 1.1 }
                    }
                }

                Repeater {
                    model: ["Dashboard", "Telemetry Debug", "AI & Voice", "API Debug"]
                    delegate: Button {
                        id: navigationButton
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        text: modelData
                        checkable: true
                        checked: window.currentPage === index
                        onClicked: window.currentPage = index
                        contentItem: Label {
                            text: navigationButton.text
                            color: navigationButton.checked ? window.textPrimary : window.textSecondary
                            font.pixelSize: 13
                            font.weight: navigationButton.checked ? Font.DemiBold : Font.Normal
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 12
                        }
                        background: Rectangle {
                            radius: 9
                            color: navigationButton.checked ? "#202831" : (navigationButton.hovered ? "#171b24" : "transparent")
                            border.color: navigationButton.checked ? "#34443f" : "transparent"
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    visible: app.mockAvailable
                    Layout.fillWidth: true
                    implicitHeight: 74
                    radius: 10
                    color: window.panel
                    border.color: window.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 11
                        spacing: 3
                        Label { text: "Developer"; color: window.textSecondary; font.pixelSize: 10 }
                        Switch {
                            text: "Mock telemetry"
                            checked: app.mockEnabled
                            onToggled: app.setUseMockTelemetry(checked)
                            palette.windowText: window.textPrimary
                            font.pixelSize: 12
                        }
                    }
                }

                Label { text: "RACE ENGINEER  •  v0.9"; color: "#596171"; font.pixelSize: 9; Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 8 }
            }
        }

        StackLayout {
            currentIndex: window.currentPage
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: 18
                    anchors.margins: 26

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        Layout.topMargin: 24
                        ColumnLayout {
                            spacing: 2
                            Label { text: "AI Race Engineer"; color: window.textPrimary; font.pixelSize: 28; font.weight: Font.DemiBold }
                            Label { text: "Low-latency telemetry for Assetto Corsa and ACC"; color: window.textSecondary; font.pixelSize: 13 }
                        }
                        Item { Layout.fillWidth: true }
                        StatusPill { label: app.connectionText; dotColor: app.connected ? window.accent : window.warning }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        implicitHeight: 94
                        radius: 14
                        color: window.panel
                        border.color: app.connected ? "#27533f" : window.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 16
                            Rectangle {
                                Layout.preferredWidth: 52; Layout.preferredHeight: 52; radius: 14
                                color: app.connected ? "#173b2d" : "#242936"
                                Label { anchors.centerIn: parent; text: app.connected ? "●" : "○"; color: app.connected ? window.accent : window.textSecondary; font.pixelSize: 21 }
                            }
                            ColumnLayout {
                                spacing: 3
                                Caption { text: "SIMULATOR" }
                                Label { text: app.simulatorName; color: window.textPrimary; font.pixelSize: 18; font.weight: Font.DemiBold }
                                Label { text: window.available(window.telemetry.track) ? window.telemetry.track : "Launch AC or ACC to connect"; color: window.textSecondary; font.pixelSize: 12 }
                            }
                            Item { Layout.fillWidth: true }
                            ColumnLayout {
                                visible: app.connected
                                spacing: 2
                                Caption { text: "SESSION"; Layout.alignment: Qt.AlignRight }
                                Label { text: window.available(window.telemetry.sessionType) ? window.telemetry.sessionType : "Unavailable"; color: window.textPrimary; font.pixelSize: 14; Layout.alignment: Qt.AlignRight }
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        columns: width > 790 ? 4 : 2
                        rowSpacing: 12
                        columnSpacing: 12
                        MetricCard { Layout.fillWidth: true; caption: "Speed"; value: window.number(window.telemetry.speedKmh, 1, " km/h") }
                        MetricCard { Layout.fillWidth: true; caption: "Engine"; value: window.integer(window.telemetry.rpm, "") + (window.available(window.telemetry.rpm) ? " rpm" : ""); detail: "Gear " + (window.available(window.telemetry.gear) ? (window.telemetry.gear === 0 ? "N" : window.telemetry.gear) : "—") }
                        MetricCard { Layout.fillWidth: true; caption: "Fuel"; value: window.number(window.telemetry.fuelLiters, 1, " L"); detail: window.available(window.telemetry.fuelCapacityLiters) ? "Capacity " + Number(window.telemetry.fuelCapacityLiters).toFixed(0) + " L" : "Capacity unavailable" }
                        MetricCard { Layout.fillWidth: true; caption: "Position"; value: window.integer(window.telemetry.position, "P"); detail: window.available(window.telemetry.currentLap) ? "Lap " + window.telemetry.currentLap + (window.available(window.telemetry.totalLaps) ? " / " + window.telemetry.totalLaps : "") : "Lap data unavailable" }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 204
                            radius: 14
                            color: window.panel
                            border.color: window.border
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 17; spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: "Voice assistant"; color: window.textPrimary; font.pixelSize: 15; font.weight: Font.DemiBold }
                                    Item { Layout.fillWidth: true }
                                    StatusPill { label: app.voiceStatus; dotColor: app.voiceStatus === "Listening" ? window.accent : window.textSecondary }
                                }
                                Label { text: app.microphoneName; color: window.textSecondary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                ProgressBar {
                                    Layout.fillWidth: true
                                    from: 0; to: 1; value: app.microphoneLevel
                                }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: window.border }
                                RowLayout {
                                    Label { text: "TEN VAD • 16 KHZ"; color: window.textSecondary; font.pixelSize: 10 }
                                    Label { text: "GWEN-TTS • " + app.ttsStatus.toUpperCase(); color: app.ttsAvailable ? window.accent : window.warning; font.pixelSize: 10 }
                                    Item { Layout.fillWidth: true }
                                    Button {
                                        text: app.keyboardPttEnabled ? "Hold to talk  Ctrl+Space" : "Keyboard PTT disabled"
                                        enabled: app.keyboardPttEnabled
                                        onPressed: app.beginPushToTalk()
                                        onReleased: app.endPushToTalk()
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 204
                            radius: 14
                            color: window.panel
                            border.color: window.border
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 17; spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: "AI provider"; color: window.textPrimary; font.pixelSize: 15; font.weight: Font.DemiBold }
                                    Item { Layout.fillWidth: true }
                                    StatusPill { label: app.apiState; dotColor: app.apiState === "Connected" ? window.accent : window.warning }
                                }
                                Label { text: app.apiConfigured ? app.apiDetail : "Configure the local LLM endpoint in AI & Voice. Telemetry and alerts remain local."; color: window.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: window.border }
                                RowLayout {
                                    Label { text: "DEFAULT"; color: window.textSecondary; font.pixelSize: 10 }
                                    Item { Layout.fillWidth: true }
                                    Label { text: app.apiProvider + " • " + app.apiModel; color: window.textPrimary; font.pixelSize: 13; elide: Text.ElideRight }
                                }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        implicitHeight: interactionContent.implicitHeight + 34
                        radius: 14; color: window.panel; border.color: window.border
                        ColumnLayout {
                            id: interactionContent
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 17; spacing: 9
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Latest interaction"; color: window.textPrimary; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Item { Layout.fillWidth: true }
                                Button { text: "Reset"; onClicked: app.resetConversation() }
                            }
                            Caption { text: "DRIVER" }
                            Label { text: app.latestUserText.length ? app.latestUserText : "Ask by voice or type from AI & Voice."; color: app.latestUserText.length ? window.textPrimary : window.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Caption { text: "ENGINEER" }
                            Label { text: app.latestEngineerText.length ? app.latestEngineerText : "Waiting for a question."; color: app.latestEngineerText.length ? window.accent : window.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                    Item { Layout.preferredHeight: 20 }
                }
            }

            ScrollView {
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 26
                        Layout.bottomMargin: 4
                        ColumnLayout {
                            Label { text: "Telemetry Debug"; color: window.textPrimary; font.pixelSize: 27; font.weight: Font.DemiBold }
                            Label { text: "Normalized snapshot • refreshed at 10 Hz"; color: window.textSecondary; font.pixelSize: 12 }
                        }
                        Item { Layout.fillWidth: true }
                        StatusPill { label: app.simulatorName; dotColor: app.connected ? window.accent : window.warning }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        columns: width > 790 ? 4 : 2
                        columnSpacing: 12; rowSpacing: 12
                        MetricCard { Layout.fillWidth: true; caption: "Speed"; value: window.number(window.telemetry.speedKmh, 1, " km/h") }
                        MetricCard { Layout.fillWidth: true; caption: "RPM"; value: window.integer(window.telemetry.rpm, "") }
                        MetricCard { Layout.fillWidth: true; caption: "Gear"; value: window.available(window.telemetry.gear) ? (window.telemetry.gear < 0 ? "R" : window.telemetry.gear === 0 ? "N" : window.telemetry.gear) : "—" }
                        MetricCard { Layout.fillWidth: true; caption: "Fuel"; value: window.number(window.telemetry.fuelLiters, 2, " L") }
                        MetricCard { Layout.fillWidth: true; caption: "Throttle"; value: window.available(window.telemetry.throttle) ? (window.telemetry.throttle * 100).toFixed(0) + "%" : "—" }
                        MetricCard { Layout.fillWidth: true; caption: "Brake"; value: window.available(window.telemetry.brake) ? (window.telemetry.brake * 100).toFixed(0) + "%" : "—" }
                        MetricCard { Layout.fillWidth: true; caption: "Lap"; value: window.integer(window.telemetry.currentLap, ""); detail: "Current " + window.time(window.telemetry.currentLapTimeSeconds) }
                        MetricCard { Layout.fillWidth: true; caption: "Position"; value: window.integer(window.telemetry.position, "P") }
                        MetricCard { Layout.fillWidth: true; caption: "Gap ahead"; value: window.number(window.telemetry.gapAheadSeconds, 2, " s") }
                        MetricCard { Layout.fillWidth: true; caption: "Gap behind"; value: window.number(window.telemetry.gapBehindSeconds, 2, " s") }
                        MetricCard { Layout.fillWidth: true; caption: "Flag"; value: window.available(window.telemetry.flag) ? window.telemetry.flag : "—" }
                        MetricCard { Layout.fillWidth: true; caption: "Pit state"; value: window.available(window.telemetry.pitState) ? window.telemetry.pitState : "—"; detail: window.available(window.telemetry.pitLimiter) ? "Limiter " + (window.telemetry.pitLimiter ? "ON" : "OFF") : "Limiter unavailable" }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        implicitHeight: tyreContent.implicitHeight + 34
                        radius: 14; color: window.panel; border.color: window.border

                        ColumnLayout {
                            id: tyreContent
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 17; spacing: 13
                            Label { text: "Tyres"; color: window.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Repeater {
                                    model: ["FL", "FR", "RL", "RR"]
                                    delegate: Rectangle {
                                        id: tyreCard
                                        required property int index
                                        required property string modelData
                                        Layout.fillWidth: true; implicitHeight: 96; radius: 10
                                        color: window.panelRaised; border.color: window.border
                                        ColumnLayout {
                                            anchors.centerIn: parent; spacing: 4
                                            Label { text: tyreCard.modelData; color: window.accent; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                                            Label {
                                                text: window.available(window.telemetry.tyreTemperatures) ? Number(window.telemetry.tyreTemperatures[tyreCard.index]).toFixed(1) + " °C" : "— °C"
                                                color: window.textPrimary; font.pixelSize: 14; Layout.alignment: Qt.AlignHCenter
                                            }
                                            Label {
                                                text: window.available(window.telemetry.tyrePressures) ? Number(window.telemetry.tyrePressures[tyreCard.index]).toFixed(1) + " psi" : "— psi"
                                                color: window.textSecondary; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true; Layout.leftMargin: 26; Layout.rightMargin: 26
                        implicitHeight: 180; radius: 14; color: window.panel; border.color: window.border
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 8
                            Label { text: "Event log"; color: window.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                            ListView {
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                                model: app.eventLog
                                delegate: RowLayout {
                                    required property var modelData
                                    width: ListView.view.width; spacing: 10
                                    Label { text: modelData.timestamp; color: window.textSecondary; font.pixelSize: 10 }
                                    Label { text: modelData.message; color: modelData.priority >= 4 ? window.warning : window.textPrimary; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                }
                                Label { anchors.centerIn: parent; visible: app.eventLog.length === 0; text: "No state-transition events yet."; color: window.textSecondary }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        Layout.bottomMargin: 26
                        implicitHeight: 64
                        radius: 10; color: "#10141c"; border.color: window.border
                        Label {
                            anchors.fill: parent; anchors.margins: 14
                            text: "Unavailable values are intentionally shown as —. Lap, position, gap, flag and pit state come from the graphics page. ACC opponent names and pace additionally require updListenerPort to be enabled in broadcasting.json."
                            color: window.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            ScrollView {
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true; Layout.margins: 26; Layout.bottomMargin: 4
                        ColumnLayout {
                            Label { text: "AI & Voice"; color: window.textPrimary; font.pixelSize: 27; font.weight: Font.DemiBold }
                            Label { text: "OpenAI-compatible llama.cpp server • local, LAN, Tailscale or cloud"; color: window.textSecondary; font.pixelSize: 12 }
                        }
                        Item { Layout.fillWidth: true }
                        StatusPill { label: app.apiState; dotColor: app.apiState === "Connected" ? window.accent : window.warning }
                    }

                    Rectangle {
                        Layout.fillWidth: true; Layout.leftMargin: 26; Layout.rightMargin: 26
                        implicitHeight: aiSettingsContent.implicitHeight + 34
                        radius: 14; color: window.panel; border.color: window.border
                        ColumnLayout {
                            id: aiSettingsContent
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 17; spacing: 10
                            Label { text: "AI provider"; color: window.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                            GridLayout {
                                Layout.fillWidth: true; columns: 2; columnSpacing: 14; rowSpacing: 9
                                Caption { text: "PROVIDER" }
                                ComboBox {
                                    id: providerField; Layout.fillWidth: true
                                    model: ["OpenAI Compatible"]
                                    currentIndex: 0
                                }
                                Caption { text: "API BASE URL" }
                                TextField { id: baseUrlField; Layout.fillWidth: true; text: app.apiBaseUrl; placeholderText: "http://100.114.125.88:8080/v1" }
                                Caption { text: "API KEY (OPTIONAL)" }
                                TextField { id: apiKeyField; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "Optional; leave blank for no Authorization header" }
                                Caption { text: "MODEL" }
                                TextField { id: modelField; Layout.fillWidth: true; text: app.apiModel; placeholderText: "race-engineer" }
                                Caption { text: "TIMEOUT (MS)" }
                                TextField { id: timeoutField; Layout.fillWidth: true; text: app.apiTimeoutMilliseconds; inputMethodHints: Qt.ImhDigitsOnly }
                                Caption { text: "MAX TOKENS" }
                                TextField { id: tokenField; Layout.fillWidth: true; text: app.apiMaximumTokens; inputMethodHints: Qt.ImhDigitsOnly }
                                Caption { text: "TEMPERATURE" }
                                TextField { id: temperatureField; Layout.fillWidth: true; text: app.apiTemperature; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                                Caption { text: "STREAMING" }
                                Switch { id: streamingField; checked: app.apiStreaming; text: checked ? "Enabled" : "Disabled" }
                            }
                            Label { text: app.apiDetail; color: app.apiState === "Connected" ? window.accent : window.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            RowLayout {
                                Item { Layout.fillWidth: true }
                                Button {
                                    text: "Save"
                                    onClicked: app.saveAiSettings(providerField.currentText, baseUrlField.text, apiKeyField.text,
                                                                   modelField.text, streamingField.checked,
                                                                   Number(timeoutField.text), Number(tokenField.text), Number(temperatureField.text))
                                }
                                Button { text: "Test /models"; enabled: baseUrlField.text.length > 0 && modelField.text.length > 0; onClicked: { app.saveAiSettings(providerField.currentText, baseUrlField.text, apiKeyField.text, modelField.text, streamingField.checked, Number(timeoutField.text), Number(tokenField.text), Number(temperatureField.text)); app.testApiConnection() } }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true; Layout.leftMargin: 26; Layout.rightMargin: 26
                        implicitHeight: askContent.implicitHeight + 34
                        radius: 14; color: window.panel; border.color: window.border
                        ColumnLayout {
                            id: askContent
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 17; spacing: 10
                            Label { text: "Type a question"; color: window.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                            RowLayout {
                                Layout.fillWidth: true
                                TextField { id: questionField; Layout.fillWidth: true; placeholderText: "Do I have enough fuel to finish?"; onAccepted: { app.askText(text); text = "" } }
                                Button { text: "Ask"; enabled: questionField.text.length > 0; onClicked: { app.askText(questionField.text); questionField.text = "" } }
                            }
                            Label { text: app.latestEngineerText; visible: text.length > 0; color: window.accent; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true; Layout.leftMargin: 26; Layout.rightMargin: 26
                        implicitHeight: pttContent.implicitHeight + 34
                        radius: 14; color: window.panel; border.color: window.border
                        ColumnLayout {
                            id: pttContent
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 17; spacing: 10
                            Label { text: "Push-to-talk mapping"; color: window.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                            Label { text: "DirectInput works in the background and reads the held/released state of wheel buttons."; color: window.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            RowLayout {
                                Layout.fillWidth: true
                                CheckBox { id: keyboardPttField; text: "Ctrl+Space"; checked: app.keyboardPttEnabled }
                                CheckBox { id: directInputPttField; text: "DirectInput wheel button"; checked: app.directInputPttEnabled }
                                Item { Layout.fillWidth: true }
                                Button { text: "Apply"; onClicked: app.setPushToTalkOptions(keyboardPttField.checked, directInputPttField.checked) }
                            }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: window.border }
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 2
                                    Caption { text: "CURRENT BINDING" }
                                    Label { text: app.directInputBinding; color: app.directInputBinding === "Not mapped" ? window.warning : window.textPrimary; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: app.directInputStatus; color: window.textSecondary; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                Button { text: "Map button"; onClicked: app.beginDirectInputMapping() }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true; Layout.leftMargin: 26; Layout.rightMargin: 26; Layout.bottomMargin: 26
                        implicitHeight: 72; radius: 12; color: "#10141c"; border.color: window.border
                        Label { anchors.fill: parent; anchors.margins: 14; text: "Voice input is local: TEN VAD detects completed speech and whisper.cpp transcribes it. API failures never stop telemetry or deterministic alerts."; color: window.textSecondary; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }

            ScrollView {
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width; spacing: 16
                    RowLayout {
                        Layout.fillWidth: true; Layout.margins: 26; Layout.bottomMargin: 4
                        ColumnLayout {
                            Label { text: "API Debug"; color: window.textPrimary; font.pixelSize: 27; font.weight: Font.DemiBold }
                            Label { text: "Session counters — credentials are never displayed or logged"; color: window.textSecondary; font.pixelSize: 12 }
                        }
                        Item { Layout.fillWidth: true }
                        StatusPill { label: app.apiState; dotColor: app.apiState === "Connected" ? window.accent : window.warning }
                    }
                    GridLayout {
                        Layout.fillWidth: true; Layout.leftMargin: 26; Layout.rightMargin: 26
                        columns: width > 790 ? 4 : 2; columnSpacing: 12; rowSpacing: 12
                        MetricCard { Layout.fillWidth: true; caption: "Provider"; value: app.apiProvider; detail: app.apiModel }
                        MetricCard { Layout.fillWidth: true; caption: "HTTP status"; value: String(app.apiStatistics.lastHttpStatus || "—") }
                        MetricCard { Layout.fillWidth: true; caption: "Last latency"; value: String(app.apiStatistics.lastLatencyMs || 0) + " ms"; detail: "First token " + String(app.apiStatistics.firstTokenLatencyMs || "—") + " ms" }
                        MetricCard { Layout.fillWidth: true; caption: "Average latency"; value: String(app.apiStatistics.averageLatencyMs || 0) + " ms" }
                        MetricCard { Layout.fillWidth: true; caption: "Requests"; value: String(app.apiStatistics.requests || 0) }
                        MetricCard { Layout.fillWidth: true; caption: "Failed"; value: String(app.apiStatistics.failed || 0); detail: "Rate limits " + String(app.apiStatistics.rateLimits || 0) }
                        MetricCard { Layout.fillWidth: true; caption: "Input tokens"; value: String(app.apiStatistics.inputTokens || 0) }
                        MetricCard { Layout.fillWidth: true; caption: "Output tokens"; value: String(app.apiStatistics.outputTokens || 0) }
                        MetricCard { Layout.fillWidth: true; caption: "Last tool"; value: app.apiStatistics.lastTool || "—"; detail: app.apiStatistics.streaming ? "Streaming enabled" : "Streaming disabled" }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.leftMargin: 26; Layout.rightMargin: 26
                        implicitHeight: 200; radius: 14; color: window.panel; border.color: window.border
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 8
                            Label { text: "Tool calls"; color: window.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                            ListView {
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                                model: app.toolLog
                                delegate: ColumnLayout {
                                    required property var modelData
                                    width: ListView.view.width; spacing: 2
                                    RowLayout { Layout.fillWidth: true
                                        Label { text: modelData.name; color: window.accent; font.pixelSize: 11; font.weight: Font.DemiBold }
                                        Item { Layout.fillWidth: true }
                                        Label { text: modelData.timestamp; color: window.textSecondary; font.pixelSize: 9 }
                                    }
                                    Label { text: modelData.result; color: window.textSecondary; font.family: "Consolas"; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                Label { anchors.centerIn: parent; visible: app.toolLog.length === 0; text: "No local telemetry tool has been called yet."; color: window.textSecondary }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.leftMargin: 26; Layout.rightMargin: 26; Layout.bottomMargin: 26
                        implicitHeight: 76; radius: 12; color: window.panel; border.color: window.border
                        ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 4
                            Caption { text: "STATUS DETAIL" }
                            Label { text: app.apiDetail; color: window.textPrimary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    }
}
