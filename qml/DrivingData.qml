pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: vehicleData
    required property UiTheme theme
    required property var telemetry
    property real availableHeight: height
    property real textScale: 1
    property bool wheelsOnly: false
    readonly property bool compact: width < 1100 || (!wheelsOnly && availableHeight < 380)
    readonly property bool tight: compact && availableHeight < 340
    readonly property bool dense: compact || availableHeight < 620
    readonly property real panelWidth: Math.max(0, (width - 2) / 3.4)
    readonly property real minimumInstrumentHeight: compact ? 280 : 420
    readonly property real speedFontSize: compact ? (tight ? 56 : 76) : availableHeight < 500 ? 148 : availableHeight < 600 ? 176 : 196
    readonly property real fuelFontSize: compact ? (tight ? 42 : 56) : availableHeight < 500 ? 80 : availableHeight < 600 ? 96 : 108
    implicitHeight: minimumInstrumentHeight
    function fontSize(baseSize) { return Math.round(baseSize * textScale) }

    component Label: Text {
        color: vehicleData.theme.muted
        font.family: vehicleData.theme.uiFont
        font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 13 : 15)
        font.weight: vehicleData.theme.uiWeight
        textFormat: Text.PlainText
    }
    component Value: Label {
        color: vehicleData.theme.text
        font.family: vehicleData.theme.monoFont
        font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 20 : 30)
        font.weight: vehicleData.theme.monoWeight
    }
    component Rule: Rectangle { color: vehicleData.theme.divider; Layout.fillWidth: true; implicitHeight: 1 }
    component Pedal: ColumnLayout {
        id: pedal
        required property string label
        property var value
        readonly property real fraction: vehicleData.theme.hasNumber(value) ? Math.max(0, Math.min(1, Number(value))) : 0
        Layout.fillWidth: true
        spacing: vehicleData.compact ? 4 : 8
        Label { text: pedal.label }
        RowLayout {
            Layout.fillWidth: true
            Value { text: vehicleData.theme.number(vehicleData.theme.hasNumber(pedal.value) ? Number(pedal.value) * 100 : undefined, 0, "%"); font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 22 : vehicleData.dense ? 40 : 48); Layout.fillWidth: true }
            Column {
                width: vehicleData.compact ? 16 : 34
                height: vehicleData.tight ? 40 : vehicleData.compact ? 64 : Math.max(64, Math.min(160, vehicleData.availableHeight - vehicleData.speedFontSize * 1.3 - 190))
                spacing: 2
                Repeater {
                    model: 12
                    Rectangle {
                        id: pedalSegment
                        required property int index
                        width: parent.width
                        height: (parent.height - 22) / 12
                        color: vehicleData.theme.raised
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: parent.height * Math.max(0, Math.min(1, pedal.fraction * 12 - (11 - pedalSegment.index)))
                            color: vehicleData.theme.accent
                        }
                    }
                }
            }
        }
    }
    component Reading: RowLayout {
        id: reading
        required property string label
        required property string value
        property string compactLabel: label
        property string labelObjectName: ""
        Layout.fillWidth: true
        spacing: vehicleData.compact ? 6 : vehicleData.dense ? 8 : 12
        Label {
            objectName: reading.labelObjectName
            text: vehicleData.compact && vehicleData.textScale <= 1 ? reading.compactLabel : reading.label
            Accessible.name: reading.label
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            wrapMode: vehicleData.textScale > 1 ? Text.WordWrap : Text.NoWrap
            elide: vehicleData.textScale > 1 ? Text.ElideNone : Text.ElideRight
            ToolTip.visible: hovered.hovered
            ToolTip.text: reading.label
            HoverHandler { id: hovered }
        }
        Value { text: reading.value; font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 16 : vehicleData.dense ? 20 : 25); horizontalAlignment: Text.AlignRight }
    }

    Row {
        anchors.fill: parent

        Item {
            visible: !vehicleData.wheelsOnly
            width: visible ? vehicleData.panelWidth : 0
            height: parent.height
            ColumnLayout {
                id: leftInstrumentLayout
                anchors.fill: parent
                anchors.margins: vehicleData.tight ? 6 : vehicleData.compact ? 8 : vehicleData.dense ? 10 : 14
                spacing: vehicleData.tight ? 3 : vehicleData.compact ? 6 : vehicleData.dense ? 8 : 12
                Label { text: "TỐC ĐỘ" }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        RowLayout {
                            spacing: 6
                            Value {
                            objectName: "speedValue"
                            text: vehicleData.theme.number(vehicleData.telemetry.speedKmh, 0, "")
                            font.family: vehicleData.theme.displayFont
                            font.weight: vehicleData.theme.displayWeight
                            font.pixelSize: vehicleData.fontSize(vehicleData.speedFontSize)
                        }
                            Label { text: "km/h"; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: vehicleData.compact ? 8 : 16 }
                        }
                    }
                    ColumnLayout {
                        spacing: vehicleData.compact ? 8 : vehicleData.dense ? 6 : 8
                        Label { text: "SỐ" }
                        Rectangle {
                            Layout.preferredWidth: vehicleData.compact ? 60 : vehicleData.width < 1350 ? 80 : 100
                            Layout.preferredHeight: vehicleData.tight ? 62 : vehicleData.compact ? 82 : vehicleData.dense ? 90 : 118
                            color: "transparent"
                            border.color: vehicleData.theme.outline
                            Value {
                                objectName: "gearValue"
                                anchors.centerIn: parent
                                text: vehicleData.theme.gear(vehicleData.telemetry.gear)
                                font.family: vehicleData.theme.displayFont
                                font.weight: vehicleData.theme.displayWeight
                                font.pixelSize: vehicleData.fontSize(vehicleData.tight ? 42 : vehicleData.compact ? 52 : vehicleData.dense ? 82 : 100)
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "RPM" }
                    Value { text: vehicleData.theme.number(vehicleData.telemetry.rpm, 0, ""); font.pixelSize: vehicleData.fontSize(17) }
                    Item { Layout.fillWidth: true }
                }
                Row {
                    Layout.fillWidth: true
                    spacing: 3
                    Repeater {
                        model: 24
                        Rectangle {
                            required property int index
                            width: (parent.width - 23 * 3) / 24
                            height: vehicleData.tight ? 12 : vehicleData.compact ? 15 : vehicleData.dense ? 18 : 24
                            color: vehicleData.theme.hasNumber(vehicleData.telemetry.rpm) && index < Number(vehicleData.telemetry.rpm) / 9000 * 24
                                ? vehicleData.theme.accent : vehicleData.theme.raised
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Repeater {
                        model: vehicleData.compact ? ["0", "4.000", "9.000+"] : ["0", "2.000", "4.000", "6.000", "8.000", "9.000+"]
                        Label {
                            required property int index
                            required property string modelData
                            text: modelData
                            font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 11 : 12)
                            Layout.fillWidth: true
                            horizontalAlignment: index === 0 ? Text.AlignLeft : index === (vehicleData.compact ? 2 : 5) ? Text.AlignRight : Text.AlignHCenter
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: vehicleData.compact ? 12 : vehicleData.dense ? 8 : 18
                    Pedal { label: "GA"; value: vehicleData.telemetry.throttle }
                    Pedal { label: "PHANH"; value: vehicleData.telemetry.brake }
                }
            }
        }

        Rectangle { visible: !vehicleData.wheelsOnly; width: visible ? 1 : 0; height: parent.height; color: vehicleData.theme.divider }

        GridLayout {
            id: tyreTileGrid
            objectName: "tyreTileGrid"
            width: vehicleData.wheelsOnly ? vehicleData.width : vehicleData.panelWidth * 1.4
            height: parent.height
            columns: 2
            rowSpacing: vehicleData.compact ? 8 : 14
            columnSpacing: vehicleData.compact ? 8 : 14
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: [
                    { code: "FL", dataIndex: 0 }, { code: "FR", dataIndex: 1 },
                    { code: "RL", dataIndex: 2 }, { code: "RR", dataIndex: 3 }
                ]
                delegate: Rectangle {
                    id: tyreTile
                    required property int index
                    required property var modelData
                    objectName: "tyreTile" + modelData.code
                    readonly property int dataIndex: modelData.dataIndex
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    Layout.minimumHeight: 0
                    Layout.preferredWidth: 150
                    Layout.preferredHeight: vehicleData.compact ? 126 : 160
                    color: vehicleData.theme.canvas
                    border.color: vehicleData.theme.outline
                    border.width: 1
                    radius: 4
                    Accessible.name: modelData.code + ", nhiệt độ lốp " + temperatureValue.text + ", áp suất " + pressureValue.text + ", nhiệt độ phanh " + brakeTemperatureValue.text
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: vehicleData.compact ? 6 : 12
                        spacing: vehicleData.compact ? 4 : 8
                        Label {
                            objectName: "wheelLabel" + tyreTile.modelData.code
                            text: tyreTile.modelData.code
                            color: vehicleData.theme.text
                            font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 13 : 15)
                            font.weight: vehicleData.theme.sectionWeight
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: vehicleData.theme.divider }
                        Item { Layout.fillHeight: true }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                objectName: "wheelTemperatureCaption" + tyreTile.modelData.code
                                text: "LỐP"
                                font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 13 : 15)
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                wrapMode: Text.NoWrap
                                elide: Text.ElideNone
                            }
                            Value {
                                id: temperatureValue
                                objectName: "wheelTemperature" + tyreTile.modelData.code
                                text: vehicleData.theme.number(vehicleData.telemetry.tyreTemperatures
                                    ? vehicleData.telemetry.tyreTemperatures[tyreTile.dataIndex] : undefined, 0, "°C")
                                font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 16 : 22)
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                objectName: "wheelPressureCaption" + tyreTile.modelData.code
                                text: "ÁP SUẤT"
                                font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 13 : 15)
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                wrapMode: Text.NoWrap
                                elide: Text.ElideNone
                            }
                            Value {
                                id: pressureValue
                                objectName: "wheelPressure" + tyreTile.modelData.code
                                text: vehicleData.theme.number(vehicleData.telemetry.tyrePressures
                                    ? vehicleData.telemetry.tyrePressures[tyreTile.dataIndex] : undefined, 1, " psi")
                                font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 12 : 14)
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                objectName: "wheelBrakeTemperatureCaption" + tyreTile.modelData.code
                                text: "PHANH"
                                font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 13 : 15)
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                wrapMode: Text.NoWrap
                                elide: Text.ElideNone
                            }
                            Value {
                                id: brakeTemperatureValue
                                objectName: "wheelBrakeTemperature" + tyreTile.modelData.code
                                text: vehicleData.theme.number(vehicleData.telemetry.brakeTemperatures
                                    ? vehicleData.telemetry.brakeTemperatures[tyreTile.dataIndex] : undefined, 0, "°C")
                                font.pixelSize: vehicleData.fontSize(vehicleData.compact ? 16 : 22)
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }

        Rectangle { visible: !vehicleData.wheelsOnly; width: visible ? 1 : 0; height: parent.height; color: vehicleData.theme.divider }

        Item {
            visible: !vehicleData.wheelsOnly
            width: visible ? vehicleData.panelWidth : 0
            height: parent.height
            ColumnLayout {
                id: fuelInstrumentLayout
                anchors.fill: parent
                anchors.margins: vehicleData.tight ? 6 : vehicleData.compact ? 8 : vehicleData.dense ? 10 : 14
                spacing: vehicleData.tight ? 3 : vehicleData.compact ? 6 : vehicleData.dense ? 8 : 18
                Label { text: "NHIÊN LIỆU CÒN LẠI" }
                Value {
                    objectName: "fuelValue"
                    text: vehicleData.theme.number(vehicleData.telemetry.fuelLiters, 1, " L")
                    font.family: vehicleData.theme.displayFont
                    font.weight: vehicleData.theme.displayWeight
                    font.pixelSize: vehicleData.fontSize(vehicleData.fuelFontSize)
                }
                Rule {}
                Reading { labelObjectName: "fuelPerLapLabel"; label: "Tiêu thụ trung bình"; compactLabel: "Tiêu thụ TB"; value: vehicleData.theme.number(vehicleData.telemetry.fuelPerLapLiters, 2, " L/vòng") }
                Reading { label: "Ước tính còn"; compactLabel: "Ước tính còn"; value: vehicleData.theme.number(vehicleData.telemetry.estimatedFuelLaps, 1, " vòng") }
                Rule {}
                Reading { label: "Xe trước cách"; value: vehicleData.theme.number(vehicleData.telemetry.gapAheadSeconds, 3, " s") }
                Reading { label: "Xe sau cách"; value: vehicleData.theme.number(vehicleData.telemetry.gapBehindSeconds, 3, " s") }
                Item { Layout.fillHeight: true }
                Rule {}
                Reading { label: "Vòng trước"; value: vehicleData.theme.lap(vehicleData.telemetry.previousLapTimeSeconds) }
                Reading { label: "Tốt nhất"; value: vehicleData.theme.lap(vehicleData.telemetry.bestLapTimeSeconds) }
            }
        }
    }
}
