pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page
    objectName: "telemetryPage"
    required property QtObject controller
    required property UiTheme theme
    required property bool live
    readonly property var telemetry: live ? controller.telemetry : ({})
    readonly property var snapshot: telemetry
    readonly property bool compact: width < 1040
    contentWidth: availableWidth
    contentHeight: content.implicitHeight + 48
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    component Caption: Text {
        color: page.theme.muted
        font.family: page.theme.uiFont
        font.pixelSize: page.theme.pageTextSize(14)
        font.weight: page.theme.uiWeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component NumberValue: Caption {
        color: page.theme.text
        font.family: page.theme.monoFont
        font.pixelSize: page.theme.pageTextSize(24)
        font.weight: page.theme.monoWeight
    }
    component Rule: Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: page.theme.divider }
    component Meter: Item {
        id: meter
        property var value
        property int segments: 32
        readonly property real fraction: page.theme.hasNumber(value) ? Math.max(0, Math.min(1, Number(value))) : 0
        implicitHeight: 22
        Row {
            anchors.fill: parent
            spacing: 3
            Repeater {
                model: meter.segments
                Rectangle {
                    id: segment
                    required property int index
                    width: Math.max(0, (meter.width - (meter.segments - 1) * 3) / meter.segments)
                    height: meter.height
                    color: page.theme.raised
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, meter.fraction * meter.segments - segment.index))
                        height: parent.height
                        color: page.theme.accent
                    }
                }
            }
        }
    }
    component PedalRow: ColumnLayout {
        id: pedal
        required property string label
        property var value
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            Caption { text: pedal.label; Layout.preferredWidth: page.compact ? 92 : 124 }
            NumberValue { text: page.theme.number(page.theme.hasNumber(pedal.value) ? Number(pedal.value) * 100 : undefined, 0, "%"); Layout.preferredWidth: 78 }
            Meter { Layout.fillWidth: true; value: pedal.value }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.preferredWidth: (page.compact ? 92 : 124) + 84 }
            Caption { text: "0%" }
            Item { Layout.fillWidth: true }
            Caption { text: "50%" }
            Item { Layout.fillWidth: true }
            Caption { text: "100%" }
        }
    }
    component Opponent: ColumnLayout {
        id: opponent
        required property bool ahead
        readonly property string opponentName: String((ahead ? page.telemetry.opponentAhead : page.telemetry.opponentBehind) || "")
        Layout.fillWidth: true
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Caption { text: opponent.ahead ? "XE TRƯỚC" : "XE SAU"; Layout.fillWidth: true }
            NumberValue {
                objectName: opponent.ahead ? "opponentAheadGap" : "opponentBehindGap"
                text: page.theme.number(opponent.ahead ? page.telemetry.gapAheadSeconds : page.telemetry.gapBehindSeconds, 3, " s")
            }
        }
        Caption {
            objectName: opponent.ahead ? "opponentAheadName" : "opponentBehindName"
            text: opponent.opponentName || (opponent.ahead ? "Chưa có xe trước" : "Chưa có xe sau")
            color: page.theme.text
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
            Accessible.name: (opponent.ahead ? "Xe trước: " : "Xe sau: ") + text
        }
    }
    component LapReading: ColumnLayout {
        id: lap
        required property string label
        required property string value
        required property string valueName
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Caption { text: lap.label; Layout.fillWidth: true }
        NumberValue {
            objectName: lap.valueName
            text: lap.value
            font.pixelSize: page.theme.pageTextSize(page.compact ? 24 : 32)
            Layout.fillWidth: true
        }
    }

    ColumnLayout {
        id: content
        x: 24; y: 24
        width: page.availableWidth - 48
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 24
            Caption {
                text: "Telemetry"
                color: page.theme.text
                font.family: page.theme.displayFont
                font.weight: page.theme.displayWeight
                font.pixelSize: page.theme.pageTextSize(page.compact ? 32 : 52)
            }
            ColumnLayout {
                Layout.fillWidth: true
                Caption { text: (page.telemetry.track || "Chưa có đường đua") + " · " + (page.telemetry.carModel || "Chưa có dữ liệu xe"); Layout.fillWidth: true }
                Caption { text: page.controller.connectionText; Layout.fillWidth: true }
            }
        }
        Rule {}
        GridLayout {
            objectName: "telemetryMainSection"
            Layout.fillWidth: true
            columns: page.compact ? 1 : 2
            columnSpacing: 32
            rowSpacing: 24
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 3
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 20
                    ColumnLayout {
                        Caption { text: "SỐ" }
                        Rectangle {
                            implicitWidth: 90; implicitHeight: 108
                            color: page.theme.canvas
                            border.color: page.theme.outline
                            NumberValue {
                                objectName: "telemetryGearValue"
                                anchors.centerIn: parent
                                text: page.theme.gear(page.telemetry.gear)
                                font.family: page.theme.displayFont
                                font.pixelSize: page.theme.pageTextSize(72)
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Caption { text: "TỐC ĐỘ" }
                        RowLayout {
                            NumberValue {
                                objectName: "telemetrySpeedValue"
                                text: page.theme.number(page.telemetry.speedKmh, 0, "")
                                font.family: page.theme.displayFont
                                font.pixelSize: page.theme.pageTextSize(page.width >= 1450 ? 176 : 120)
                            }
                            Caption { text: "km/h"; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 20 }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Caption { text: "RPM" }
                    NumberValue { objectName: "telemetryRpmValue"; text: page.theme.number(page.telemetry.rpm, 0, ""); font.pixelSize: page.theme.pageTextSize(20) }
                    Item { Layout.fillWidth: true }
                }
                Meter { objectName: "telemetryRpmMeter"; Layout.fillWidth: true; value: page.theme.hasNumber(page.telemetry.rpm) ? Number(page.telemetry.rpm) / 9000 : undefined }
                RowLayout {
                    Layout.fillWidth: true
                    Repeater {
                        model: ["0", "2.000", "4.000", "6.000", "8.000", "9.000+"]
                        Caption {
                            required property int index
                            required property string modelData
                            text: modelData
                            font.pixelSize: page.theme.pageTextSize(12)
                            Layout.fillWidth: true
                            horizontalAlignment: index === 0 ? Text.AlignLeft : index === 5 ? Text.AlignRight : Text.AlignHCenter
                        }
                    }
                }
                PedalRow { label: "GA (THROTTLE)"; value: page.telemetry.throttle }
                PedalRow { label: "PHANH (BRAKE)"; value: page.telemetry.brake }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 2
                Layout.alignment: Qt.AlignTop
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        Caption { text: "VỊ TRÍ HIỆN TẠI"; Layout.fillWidth: true }
                        NumberValue {
                            objectName: "telemetryPositionValue"
                            text: page.theme.hasNumber(page.telemetry.position) ? "P" + String(page.telemetry.position).padStart(2, "0") : "P—"
                            font.family: page.theme.displayFont
                            font.pixelSize: page.theme.pageTextSize(72)
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        Caption { text: "VÒNG"; Layout.fillWidth: true }
                        NumberValue {
                            objectName: "telemetryCurrentLapValue"
                            text: page.theme.number(page.telemetry.currentLap, 0, "")
                            font.family: page.theme.displayFont
                            font.pixelSize: page.theme.pageTextSize(72)
                        }
                    }
                }
                Rule {}
                RowLayout {
                    Layout.fillWidth: true
                    Caption { text: "TIẾN ĐỘ VÒNG"; Layout.fillWidth: true }
                    NumberValue { objectName: "telemetryProgressValue"; text: page.theme.number(page.theme.hasNumber(page.telemetry.lapProgress) ? Number(page.telemetry.lapProgress) * 100 : undefined, 1, "%") }
                }
                Meter { Layout.fillWidth: true; implicitHeight: 12; value: page.telemetry.lapProgress; segments: 1 }
                Rule {}
                Opponent { ahead: true }
                Rule {}
                Opponent { ahead: false }
                Rule {}
                Caption { text: "SỰ KIỆN MỚI NHẤT" }
                Caption { objectName: "telemetryLatestEvent"; text: page.controller.latestEvent || "Chưa có sự kiện trong phiên."; color: page.theme.text; Layout.fillWidth: true }
            }
        }
        Rule {}
        Caption { text: "THỜI GIAN VÒNG ĐUA"; font.weight: page.theme.sectionWeight }
        GridLayout {
            objectName: "telemetryLapLedger"
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 24
            LapReading { label: "Hiện tại"; value: page.theme.lap(page.telemetry.currentLapTimeSeconds); valueName: "telemetryCurrentLapTime" }
            LapReading { label: "Vòng trước"; value: page.theme.lap(page.telemetry.previousLapTimeSeconds); valueName: "telemetryPreviousLapTime" }
            LapReading { label: "Tốt nhất"; value: page.theme.lap(page.telemetry.bestLapTimeSeconds); valueName: "telemetryBestLapTime" }
        }
        Rule {}
        Caption { text: "NHIỆT ĐỘ & ÁP SUẤT BỐN BÁNH"; font.weight: page.theme.sectionWeight }
        DrivingData {
            objectName: "telemetryDrivingData"
            Layout.fillWidth: true
            Layout.preferredHeight: 360
            theme: page.theme
            telemetry: page.telemetry
            textScale: 1.15
            wheelsOnly: true
            availableHeight: height
        }
        Rule {}
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 24
            LapReading { label: "Nhiên liệu còn lại"; value: page.theme.number(page.telemetry.fuelLiters, 1, " L"); valueName: "telemetryFuelValue" }
            LapReading { label: "Tiêu thụ trung bình"; value: page.theme.number(page.telemetry.fuelPerLapLiters, 2, " L/vòng"); valueName: "telemetryFuelPerLapValue" }
            LapReading { label: "Ước tính còn"; value: page.theme.number(page.telemetry.estimatedFuelLaps, 1, " vòng"); valueName: "telemetryEstimatedLapsValue" }
        }
    }
}
