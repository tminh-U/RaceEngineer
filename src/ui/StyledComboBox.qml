import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitWidth: 224
    implicitHeight: 40
    font.family: "Segoe UI"
    font.pixelSize: 15
    leftPadding: 12
    rightPadding: 30

    contentItem: Text {
        text: control.displayText
        color: "#f3f3f5"
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        text: "⌄"
        color: "#a9abb5"
        font.pixelSize: 21
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
    }
    background: Rectangle {
        radius: 7
        color: control.pressed ? "#303136" : (control.hovered ? "#29292d" : "#202023")
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? "#0a84ff" : "#2c2d32"
    }
    delegate: ItemDelegate {
        required property string modelData
        width: ListView.view.width
        height: 40
        contentItem: Text {
            text: modelData
            color: highlighted ? "#ffffff" : "#d9d9de"
            font: control.font
            verticalAlignment: Text.AlignVCenter
            leftPadding: 10
            rightPadding: 10
            elide: Text.ElideRight
        }
        highlighted: control.highlightedIndex === index
        background: Rectangle { color: highlighted ? "#0a84ff" : "transparent"; radius: 5 }
    }
    popup: Popup {
        y: control.height + 5
        width: control.width
        implicitHeight: contentItem.implicitHeight + 8
        padding: 4
        background: Rectangle { radius: 8; color: "#242428"; border.color: "#3b3c42" }
        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, 204)
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }
}
