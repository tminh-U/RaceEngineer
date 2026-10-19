pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "startupNotice"
    required property var controller
    required property UiTheme theme
    property bool dismissed: false

    readonly property bool hasError: String(controller.startupError).length > 0
    visible: !dismissed && (!controller.startupReady || hasError)
    implicitWidth: 390
    implicitHeight: contents.implicitHeight + 28
    width: parent ? Math.min(390, Math.max(240, parent.width - 24)) : implicitWidth
    height: parent ? Math.min(implicitHeight, Math.max(0, parent.height - 28)) : implicitHeight

    component BodyText: Text {
        color: root.theme.text
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.pageTextSize(14)
        font.weight: root.theme.uiWeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component MutedText: BodyText { color: root.theme.muted; font.pixelSize: root.theme.pageTextSize(13) }
    component ActionButton: Button {
        id: control
        property bool primaryAction: false
        implicitHeight: 38
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.pageTextSize(14)
        font.weight: root.theme.sectionWeight
        Accessible.name: text
        contentItem: Text {
            text: control.text
            color: !control.enabled ? root.theme.muted : control.primaryAction ? root.theme.accentText : root.theme.text
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: !control.enabled ? root.theme.canvas : control.primaryAction ? root.theme.accent : control.hovered || control.down ? root.theme.raised : root.theme.surface
            border.color: control.activeFocus ? root.theme.outline : control.enabled ? root.theme.outline : root.theme.divider
            border.width: control.activeFocus ? 2 : 1
            radius: 3
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.theme.surface
        border.color: root.theme.divider
        border.width: 1
        radius: 4
    }
    ColumnLayout {
        id: contents
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8
        RowLayout {
            visible: root.hasError
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                objectName: "startupErrorMark"
                width: 20
                height: 20
                radius: 2
                color: "transparent"
                border.color: root.theme.outline
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "!"
                    color: root.theme.text
                    font.family: root.theme.uiFont
                    font.pixelSize: root.theme.pageTextSize(14)
                    font.weight: root.theme.displayWeight
                }
            }
            BodyText {
                objectName: "startupErrorTitle"
                Layout.fillWidth: true
                text: "Lỗi: mô hình AI chưa sẵn sàng"
                font.weight: root.theme.sectionWeight
                Accessible.name: text
            }
        }
        BodyText {
            visible: !root.hasError
            Layout.fillWidth: true
            text: "Đang chuẩn bị mô hình AI…"
            font.weight: root.theme.sectionWeight
        }
        ScrollView {
            id: errorScroll
            objectName: "startupErrorScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: errorLabel.implicitHeight
            visible: root.hasError
            clip: true
            rightPadding: 12
            contentWidth: availableWidth
            contentHeight: errorLabel.implicitHeight
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            MutedText {
                id: errorLabel
                objectName: "startupErrorLabel"
                width: errorScroll.availableWidth
                text: String(root.controller.startupError)
            }
        }
        ProgressBar {
            id: startupProgress
            objectName: "startupProgress"
            Layout.fillWidth: true
            Layout.minimumHeight: 8
            Layout.preferredHeight: 8
            visible: !root.controller.startupReady
            from: 0
            to: 1
            value: Math.max(0, Math.min(1, Number(root.controller.startupProgress)))
            background: Rectangle { color: root.theme.raised; radius: 3 }
            contentItem: Item {
                Rectangle {
                    width: parent.width * startupProgress.visualPosition
                    height: parent.height
                    radius: 3
                    color: root.theme.accent
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "startupRetryButton"
                visible: root.hasError
                text: "Thử lại"
                primaryAction: true
                onClicked: {
                    root.dismissed = false
                    root.controller.retryStartup()
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "startupCloseButton"
                text: "Đóng"
                onClicked: root.dismissed = true
            }
        }
    }
}
