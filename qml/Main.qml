pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    objectName: "appWindow"
    readonly property int availableScreenWidth: Math.max(1, Math.min(Screen.width, Screen.desktopAvailableWidth))
    readonly property int availableScreenHeight: Math.max(1, Math.min(Screen.height, Screen.desktopAvailableHeight))
    width: Math.min(1440, availableScreenWidth)
    height: Math.min(900, availableScreenHeight)
    minimumWidth: Math.min(880, availableScreenWidth)
    minimumHeight: Math.min(576, availableScreenHeight)
    visible: true
    title: "RaceEngineer"
    color: theme.canvas
    flags: Qt.Window | Qt.FramelessWindowHint
    onVisibilityChanged: function() {
        if (root.visibility === Window.Minimized && backend.minimizeToTray && backend.trayAvailable) root.hide()
    }
    onClosing: function(close) {
        if (backend.minimizeOnClose && backend.trayAvailable) {
            close.accepted = false
            root.hide()
        }
    }

    UiTheme { id: theme }
    readonly property var designTheme: theme
    readonly property bool liveWindow: root.visible && root.visibility !== Window.Minimized
    readonly property real ui: 1
    readonly property string uiFontFamily: theme.uiFont
    // Fixed wordmark/navigation budgets avoid feeding compactHeader geometry back into itself.
    readonly property real headerSingleRowMinimumWidth: 24 + 270 + 32 + 342 + 24
        + statusRow.implicitWidth + 20 + 120
    readonly property bool compactHeader: width < headerSingleRowMinimumWidth
    readonly property real headerHeight: compactHeader ? 84 : 64
    readonly property color bg: theme.canvas
    readonly property color lowest: theme.chrome
    readonly property color panel: theme.surface
    readonly property color raised: theme.raised
    readonly property color border: theme.divider
    readonly property color textMain: theme.text
    readonly property color textMuted: theme.muted
    readonly property color primary: theme.accent
    readonly property color green: theme.accent
    readonly property color amber: theme.warning
    property int currentPage: 0
    property int lastDataPage: 3
    property int lastSettingsPage: 2
    property var loadedPages: [true, false, false, false, false, false]
    property string googleVoiceDialogSource: ""
    property alias googleVoiceDialog: googleVoiceDialogHost.dialog
    readonly property int currentMode: currentPage === 0 ? 0 : [1, 3].indexOf(currentPage) >= 0 ? 1 : 2
    function openPage(page) {
        if (page < 0 || page > 5) return
        currentPage = page
        if ([1, 3].indexOf(page) >= 0) lastDataPage = page
        if ([2, 4, 5].indexOf(page) >= 0) lastSettingsPage = page
        if (!loadedPages[page]) {
            var nextLoaded = loadedPages.slice(0)
            nextLoaded[page] = true
            loadedPages = nextLoaded
        }
    }
    function openGoogleVoiceDialogFromAi() {
        googleVoiceDialogSource = "ai"
        googleVoiceDialogHost.open()
    }
    function openGoogleVoiceDialogFromSetup() {
        googleVoiceDialogSource = "setup"
        googleVoiceDialogHost.open()
    }
    function openGoogleVoiceDialog() {
        openPage(4)
        aiVoiceLoader.item.sectionIndex = 2
        openGoogleVoiceDialogFromAi()
    }
    palette.window: theme.canvas
    palette.windowText: theme.text
    palette.base: theme.chrome
    palette.text: theme.text
    palette.button: theme.raised
    palette.buttonText: theme.text
    palette.highlight: theme.accent
    palette.highlightedText: theme.accentText
    palette.alternateBase: theme.surface
    palette.brightText: theme.text
    palette.dark: theme.chrome
    palette.light: theme.raised
    palette.mid: theme.outline
    palette.midlight: theme.divider
    palette.shadow: theme.chrome
    palette.link: theme.text
    palette.linkVisited: theme.muted
    palette.placeholderText: theme.muted
    palette.toolTipBase: theme.surface
    palette.toolTipText: theme.text

    function formatTelemetryValue(value, suffix) {
        if (value === undefined || value === null || value === "") {
            return "—"
        }
        var number = Number(value)
        return isNaN(number) ? "—" : number.toFixed(3) + suffix
    }

    function apiStatusText() {
        if (!backend.apiConfigured) return "Chưa cấu hình"
        switch (backend.apiState) {
        case "Connected": return "Đã kết nối"
        case "Requesting": return "Đang gửi yêu cầu"
        case "RateLimited": return "Bị giới hạn tần suất"
        case "AuthenticationError": return "Lỗi xác thực"
        case "LLM Server Offline": return "Máy chủ ngoại tuyến"
        case "ProviderError": return "Lỗi API"
        default: return "Chưa kiểm tra kết nối"
        }
    }

    function apiStatusColor() {
        if (!backend.apiConfigured) return root.amber
        if (backend.apiState === "Connected") return root.green
        if (backend.apiState === "Requesting") return root.primary
        if (["RateLimited", "AuthenticationError", "LLM Server Offline", "ProviderError"].indexOf(backend.apiState) >= 0)
            return theme.error
        return root.amber
    }

    function voiceStatusText() {
        switch (backend.voiceStatus) {
        case "Listening": return "Mic: Đang nghe"
        case "Recognizing": return "Mic: Đang nhận dạng"
        case "Thinking": return "AI: Đang xử lý"
        case "Speaking": return "Radio: Đang phát"
        default: return backend.keyboardPttEnabled || backend.directInputPttEnabled
            ? "PTT: Sẵn sàng" : "PTT: Chưa bật"
        }
    }

    font.family: root.uiFontFamily
    font.pixelSize: 14 * ui
    font.weight: theme.uiWeight

    component BodyText: Text {
        color: root.textMain
        font.family: root.uiFontFamily
        font.pixelSize: theme.pageTextSize(14 * root.ui)
        font.weight: theme.uiWeight
        renderType: Text.QtRendering
    }
    component MutedText: BodyText { color: root.textMuted; font.pixelSize: theme.pageTextSize(13 * root.ui) }
    component HeadingText: BodyText { font.family: root.uiFontFamily; font.pixelSize: theme.pageTextSize(20 * root.ui); font.weight: theme.sectionWeight }

    component AppButton: Button {
        id: control
        implicitHeight: 38 * root.ui
        leftPadding: 16 * root.ui; rightPadding: 16 * root.ui
        font.family: root.uiFontFamily; font.weight: theme.sectionWeight; font.pixelSize: 13 * root.ui
        opacity: control.enabled ? 1.0 : 0.45
        contentItem: Text { text: control.text; color: control.highlighted ? theme.accentText : root.textMain; font: control.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle {
            radius: 4
            color: control.highlighted ? (control.down ? Qt.darker(root.primary, 1.15) : control.hovered ? Qt.lighter(root.primary, 1.08) : root.primary)
                                       : (control.down || control.hovered ? root.raised : root.panel)
            border.color: control.activeFocus ? root.primary : control.highlighted ? "transparent" : theme.outline
            border.width: control.activeFocus ? 2 : 1
        }
    }
    component StatusPill: Rectangle {
        id: pill
        property alias text: statusText.text
        property color statusColor: root.green
        property bool solidMarker: true
        implicitWidth: statusRow.implicitWidth + 20 * root.ui
        implicitHeight: 28 * root.ui
        radius: 3; color: "transparent"
        Row { id: statusRow; anchors.centerIn: parent; spacing: 7 * root.ui
            Rectangle {
                width: 8 * root.ui
                height: width
                radius: pill.solidMarker ? width / 2 : 1
                color: pill.solidMarker ? pill.statusColor : "transparent"
                border.color: pill.statusColor
                border.width: pill.solidMarker ? 0 : 1
                anchors.verticalCenter: parent.verticalCenter
            }
            BodyText { id: statusText; font.pixelSize: 12 * root.ui }
        }
    }
    component SwitchControl: Switch {
        id: toggle
        signal userToggled(bool checked)
        implicitWidth: 44; implicitHeight: 28
        padding: 0
        onToggled: userToggled(checked)
        indicator: Rectangle {
            width: 44; height: 24; y: 2; radius: 4
            color: toggle.checked ? root.primary : root.raised
            border.color: toggle.activeFocus ? root.textMain : theme.outline
            border.width: toggle.activeFocus ? 2 : 1
            Rectangle { width: 16; height: 16; radius: 2; color: toggle.checked ? theme.accentText : root.textMuted; y: 4; x: toggle.checked ? 24 : 4 }
        }
    }
    component Card: Rectangle {
        id: card
        property string title: ""
        default property alias content: body.data
        radius: 4; color: root.panel
        implicitHeight: body.implicitHeight + 34 * root.ui
        Layout.minimumHeight: implicitHeight
        Column { id: body; anchors.fill: parent; anchors.margins: 18 * root.ui; spacing: 10 * root.ui
            BodyText { visible: card.title.length > 0; text: card.title; font.family: root.uiFontFamily; font.weight: Font.DemiBold; font.pixelSize: theme.pageTextSize(15 * root.ui) }
            Rectangle { visible: card.title.length > 0; width: parent.width; height: 1; color: root.border }
        }
    }
    component SettingRow: Item {
        id: row
        property string title: ""
        property string subtitle: ""
        property string value: ""
        property bool valueInteractive: false
        property bool showSwitch: false
        property bool checked: true
        signal toggled(bool checked)
        signal valueClicked()
        implicitHeight: 54 * root.ui
        BodyText { text: row.title; anchors.left: parent.left; anchors.top: parent.top; font.family: root.uiFontFamily; font.weight: Font.DemiBold }
        MutedText { text: row.subtitle; anchors.left: parent.left; anchors.top: parent.top; anchors.topMargin: 23*root.ui; width: parent.width - 180*root.ui; elide: Text.ElideRight }
        BodyText {
            id: valueText
            visible: row.value.length > 0
            text: row.value
            color: root.primary
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
        MouseArea {
            visible: row.valueInteractive && valueText.visible
            anchors.fill: valueText
            cursorShape: Qt.PointingHandCursor
            onClicked: row.valueClicked()
        }
        SwitchControl { visible: row.showSwitch; checked: row.checked; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; onUserToggled: v => row.toggled(v) }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.border }
    }
    Rectangle {
        id: header
        width: root.width; height: root.headerHeight
        color: root.lowest
        Image {
            id: appLogo
            objectName: "raceEngineerLogo"
            x: 24; y: root.compactHeader ? 8 : 12
            width: root.compactHeader ? 32 : 40
            height: width
            source: "../assets/final_icon_64.png"
            fillMode: Image.PreserveAspectFit
            mipmap: true
        }
        Text {
            id: wordmark
            x: appLogo.x + appLogo.width + 10; y: root.compactHeader ? 7 : 12
            text: "RaceEngineer"
            font.family: theme.sansSemiBold.status === FontLoader.Ready ? theme.sansSemiBold.name : root.uiFontFamily
            font.pixelSize: root.compactHeader ? 26 : 30; font.weight: theme.sectionWeight
            color: root.textMain
        }
        Row {
            id: modeNavigation
            x: root.compactHeader ? (root.width - implicitWidth) / 2
                : Math.max(wordmark.x + wordmark.implicitWidth + 32,
                           Math.min((root.width - implicitWidth) / 2, statusRow.x - implicitWidth - 24))
            y: root.compactHeader ? 0 : 6
            spacing: 6
            Repeater {
                model: [
                    { name: "navRace", label: "Đua", mode: 0, page: 0 },
                    { name: "navAnalysis", label: "Phân tích", mode: 1, page: -1 },
                    { name: "navSettings", label: "Thiết lập", mode: 2, page: -1 }
                ]
                delegate: Button {
                    id: modeButton
                    required property int index
                    required property var modelData
                    objectName: modelData.name
                    width: root.compactHeader ? 104 : 110; height: root.compactHeader ? 42 : 50
                    text: modelData.label
                    Accessible.name: modelData.label
                    onClicked: root.openPage(modelData.mode === 0 ? 0 : modelData.mode === 1 ? root.lastDataPage : root.lastSettingsPage)
                    contentItem: Text {
                        text: modeButton.text; color: root.currentMode === modeButton.modelData.mode ? root.primary : root.textMuted
                        font.family: root.uiFontFamily; font.pixelSize: root.compactHeader ? 14 : 15
                        font.weight: root.currentMode === modeButton.modelData.mode ? theme.sectionWeight : theme.uiWeight
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: modeButton.hovered ? root.panel : "transparent"
                        border.color: modeButton.activeFocus ? root.primary : "transparent"
                        radius: 3
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; visible: root.currentMode === modeButton.modelData.mode; color: root.primary }
                    }
                }
            }
        }
        Row {
            id: statusRow
            anchors.right: root.compactHeader ? parent.right : windowButtons.left
            anchors.rightMargin: 20
            y: root.compactHeader ? 50 : 18
            spacing: 8
            StatusPill { objectName: "connectionStatus"; text: backend.connected ? "AC / ACC · Kết nối" : "Chờ AC / ACC"; statusColor: backend.connected ? root.green : root.amber; solidMarker: backend.connected; Accessible.name: text }
            StatusPill { objectName: "voiceStatus"; text: root.voiceStatusText(); statusColor: backend.voiceStatus === "Listening" ? root.green : root.textMuted; Accessible.name: text }
            StatusPill { objectName: "aiStatus"; text: "AI · " + root.apiStatusText(); statusColor: root.apiStatusColor(); Accessible.name: text }
        }
        Row {
            id: windowButtons
            anchors.right: parent.right; anchors.top: parent.top
            width: 120; height: root.compactHeader ? 44 : 56
            Repeater {
                model: ["—", "□", "×"]
                delegate: Button {
                    id: windowButton
                    required property int index
                    required property string modelData
                    width: 40; height: root.compactHeader ? 44 : 56
                    text: modelData
                    objectName: index === 0 ? "windowMinimize" : index === 1 ? "windowMaximize" : "windowClose"
                    Accessible.name: index === 0 ? "Thu nhỏ" : index === 1 ? "Phóng to / khôi phục" : "Đóng cửa sổ"
                    contentItem: Text { text: windowButton.text; color: root.textMain; font.family: root.uiFontFamily; font.pixelSize: 19; font.weight: theme.sectionWeight; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: windowButton.hovered ? root.panel : "transparent"; border.color: windowButton.activeFocus ? root.primary : "transparent" }
                    onClicked: {
                        if (index === 0) root.showMinimized()
                        else if (index === 1) root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                        else root.close()
                    }
                }
            }
        }
        DragHandler { target: null; onActiveChanged: if (active) root.startSystemMove() }
    }
    Rectangle {
        id: subnavigation
        x: 0; y: header.height; width: root.width; height: root.currentMode === 0 ? 0 : 48
        visible: height > 0
        color: root.lowest
        Row {
            x: 24; spacing: 10
            Repeater {
                model: root.currentMode === 1
                    ? [{page:3,name:"pageTelemetry",label:"Telemetry"},{page:1,name:"pageFuelStrategy",label:"Nhiên liệu & chiến thuật"}]
                    : [{page:2,name:"pageEngineerSpotter",label:"Kỹ sư & Spotter"},{page:4,name:"pageAiVoice",label:"AI & giọng nói"},{page:5,name:"pageSystemDevices",label:"Hệ thống & thiết bị"}]
                delegate: AppButton {
                    required property var modelData
                    objectName: modelData.name
                    text: modelData.label
                    Accessible.name: modelData.label
                    highlighted: root.currentPage === modelData.page
                    onClicked: root.openPage(modelData.page)
                }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.border }
    }
    Component { id: cockpitPage; Cockpit { controller: backend; theme: root.designTheme; active: root.currentPage === 0 } }
    Component { id: fuelStrategyWorkspace; FuelStrategyPage { controller: backend; theme: root.designTheme; live: root.currentPage === 1 && root.liveWindow } }
    Component { id: engineerSpotterWorkspace; EngineerSpotterPage {
        controller: backend; theme: root.designTheme; live: root.currentPage === 2 && root.liveWindow
        onOpenAiVoiceRequested: {
            root.openPage(4)
            aiVoiceLoader.item.sectionIndex = backend.ttsBackend === "Google Gemini API" ? 2 : 1
        }
    } }
    Component { id: aiVoiceWorkspace; AiVoicePage { controller: backend; theme: root.designTheme; live: root.currentPage === 4 && root.liveWindow; onOpenGoogleVoiceDialogRequested: root.openGoogleVoiceDialogFromAi() } }
    Component { id: systemDevicesWorkspace; SystemDevicesPage { controller: backend; theme: root.designTheme; live: root.currentPage === 5 && root.liveWindow } }
    Loader { objectName: "workspaceCockpit"; x: 0; y: header.height + subnavigation.height; width: root.width; height: root.height - y; active: root.loadedPages[0]; visible: root.currentPage === 0; sourceComponent: cockpitPage }
    Loader { objectName: "workspaceFuelStrategy"; x: 0; y: header.height + subnavigation.height; width: root.width; height: root.height - y; active: root.loadedPages[1]; visible: root.currentPage === 1; sourceComponent: fuelStrategyWorkspace }
    Loader { objectName: "workspaceEngineerSpotter"; x: 0; y: header.height + subnavigation.height; width: root.width; height: root.height - y; active: root.loadedPages[2]; visible: root.currentPage === 2; sourceComponent: engineerSpotterWorkspace }
    Loader { objectName: "workspaceTelemetry"; x: 0; y: header.height + subnavigation.height; width: root.width; height: root.height - y; active: root.loadedPages[3]; visible: root.currentPage === 3; sourceComponent: telemetryPage }
    Loader { id: aiVoiceLoader; objectName: "workspaceAiVoice"; x: 0; y: header.height + subnavigation.height; width: root.width; height: root.height - y; active: root.loadedPages[4]; visible: root.currentPage === 4; sourceComponent: aiVoiceWorkspace }
    Loader { objectName: "workspaceSystemDevices"; x: 0; y: header.height + subnavigation.height; width: root.width; height: root.height - y; active: root.loadedPages[5]; visible: root.currentPage === 5; sourceComponent: systemDevicesWorkspace }

    component PageScroll: ScrollView {
        clip: true; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        contentWidth: availableWidth
        background: Rectangle { color: root.bg }
    }

    Component { id: telemetryPage
        TelemetryPage {
            controller: backend
            theme: root.designTheme
            live: root.currentPage === 3 && root.liveWindow
        }
    }

    MouseArea { width:7; anchors.left:parent.left; anchors.top:parent.top; anchors.bottom:parent.bottom; cursorShape:Qt.SizeHorCursor; onPressed:root.startSystemResize(Qt.LeftEdge) }
    MouseArea { width:7; anchors.right:parent.right; anchors.top:parent.top; anchors.bottom:parent.bottom; cursorShape:Qt.SizeHorCursor; onPressed:root.startSystemResize(Qt.RightEdge) }
    MouseArea { height:7; anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom; cursorShape:Qt.SizeVerCursor; onPressed:root.startSystemResize(Qt.BottomEdge) }

    StartupNotice {
        objectName: "startupNotice"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 14
        controller: backend
        theme: root.designTheme
    }
    Component {
        id: setupWizardComponent
        SetupWizard {
            controller: backend
            theme: root.designTheme
            onOpenGoogleVoiceDialogRequested: root.openGoogleVoiceDialogFromSetup()
        }
    }
    Loader {
        id: setupWizardLoader
        objectName: "setupWizardLoader"
        anchors.fill: parent
        active: backend.setupVisible
        sourceComponent: setupWizardComponent
    }
    GoogleVoiceDialog {
        id: googleVoiceDialogHost
        controller: backend
        theme: root.designTheme
        live: root.liveWindow && ((root.googleVoiceDialogSource === "ai" && root.currentPage === 4 && aiVoiceLoader.item !== null && aiVoiceLoader.item.sectionIndex === 2)
            || (root.googleVoiceDialogSource === "setup" && setupWizardLoader.item !== null && setupWizardLoader.item.currentStep === 1))
    }


}
