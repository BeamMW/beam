import QtQuick 2.11
import QtQuick.Layouts 1.11
import "."

Item {

    id: control
    state: "off"
    
    property bool isOffButtonActive:  false
    property bool isUsdButtonActive:  false
    property bool isBtcButtonActive:  false

    property var backgroundColor:   isOffButtonActive ? Qt.rgba(141, 161, 173, 0.1) : Qt.rgba(0, 246, 210, 0.1)
    property var borderColor:       isOffButtonActive ? Style.content_secondary : Style.active
    
    readonly property var offButtonActiveColor: Style.content_secondary
    readonly property var usdButtonActiveColor: Style.active
    readonly property var btcButtonActiveColor: Style.active
    readonly property var buttonInactiveColor: Qt.rgba(0,0,0,0)
    readonly property var offButtonActiveTextColor: Style.content_main
    readonly property var usdButtonActiveTextColor: Style.content_opposite
    readonly property var btcButtonActiveTextColor: Style.content_opposite

    states: [
        State {
            name: "off";
            PropertyChanges { target: control; isOffButtonActive: true }
            PropertyChanges { target: control; isUsdButtonActive: false }
            PropertyChanges { target: control; isBtcButtonActive: false }
        },
        State {
            name: "usd";
            PropertyChanges { target: control; isOffButtonActive: false }
            PropertyChanges { target: control; isUsdButtonActive: true }
            PropertyChanges { target: control; isBtcButtonActive: false }
        },
        State {
            name: "btc";
            PropertyChanges { target: control; isOffButtonActive: false }
            PropertyChanges { target: control; isUsdButtonActive: false }
            PropertyChanges { target: control; isBtcButtonActive: true }
        }
    ]

    function getButtonWidth() {
        // control width minus left/right margins and elements spacing
        return (control.width - 6) / 3
    }

    function getButtonHeight() {
        // control height minus top and bottom margins
        return control.height - 4
    }

    Rectangle {
        anchors.fill: parent
        color: control.backgroundColor
        radius: 10
        border.width: 1
        border.color: control.borderColor
    }

    RowLayout {
        Layout.preferredWidth: control.width
        Layout.preferredHeight: control.height
        spacing: 1

        Rectangle {
            id: offRectangle
            Layout.topMargin: 2
            Layout.leftMargin: 2
            width: getButtonWidth()
            height: getButtonHeight()
            radius: 10
            color: control.isOffButtonActive ? control.offButtonActiveColor : control.buttonInactiveColor
            MouseArea { anchors.fill: parent; onClicked: control.state = "off"; }
            SFText {
                anchors.fill: parent
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: control.isOffButtonActive ? control.offButtonActiveTextColor : Style.content_disabled
                font.pixelSize: 12
                text: "Off"
            }
        }
        Rectangle {
            id: usdRectangle
            Layout.topMargin: 2
            width: getButtonWidth()
            height: getButtonHeight()
            radius: 10
            color: control.isUsdButtonActive ? control.usdButtonActiveColor : control.buttonInactiveColor
            MouseArea { anchors.fill: parent; onClicked: control.state = "usd"; }
            SFText {
                anchors.fill: parent
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: control.isUsdButtonActive ? control.usdButtonActiveTextColor : Style.content_disabled
                font.pixelSize: 12
                text: "USD"
            }
        }
        Rectangle {
            id: btcRectangle
            Layout.topMargin: 2
            Layout.rightMargin: 2
            width: getButtonWidth()
            height: getButtonHeight()
            radius: 10
            color: control.isBtcButtonActive ? control.btcButtonActiveColor : control.buttonInactiveColor
            MouseArea { anchors.fill: parent; onClicked: control.state = "btc"; }
            SFText {
                anchors.fill: parent
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: control.isBtcButtonActive ? control.btcButtonActiveTextColor : Style.content_disabled
                font.pixelSize: 12
                text: "BTC"
            }
        }
    }
}
