import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import Beam.Wallet 1.0
import "."
import "../utils.js" as Utils

Control {
    id: control

    property int available
    property int locked
    property int sending
    property int receiving

    property var onOpenExternal: null
    signal copyValueText()

    background: Rectangle {
        id:       panel
        color:    "transparent"

        Rectangle {
            width:  parent.height
            height: parent.width
            anchors.centerIn: parent
            rotation: 90
            radius:   10
            opacity:  0.3
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00458f" }
                GradientStop { position: 1.0; color: "#00B4A3" }
            }
        }
    }

    leftPadding:   20
    rightPadding:  20
    topPadding:    8
    bottomPadding: 12

    contentItem: RowLayout {
        spacing: 0

        RowLayout{
            Layout.preferredWidth: receiving > 0 || sending > 0 ? parent.width / 2 : parent.width

            BeamAmount {
                amount:            available
                spacing:           15
                lightFont:         false
                fontSize:          16
                currencySymbol:    Utils.symbolBeam
                iconSource:        "qrc:/assets/icon-beam.svg"
                iconSize:          Qt.size(22, 22)
                copyMenuEnabled:   true
                //% "Available"
                caption:           qsTrId("available-panel-available")
            }

            Item {
                Layout.fillWidth: true
            }

            BeamAmount {
                amount:            locked
                lightFont:         false
                fontSize:          16
                currencySymbol:    Utils.symbolBeam
                copyMenuEnabled:   true
                //% "Locked"
                caption:           qsTrId("available-panel-locked")
                visible:           locked > 0
            }

            Item {
                Layout.fillWidth: true
            }
        }

        RowLayout{
            Layout.preferredWidth: parent.width / 2
            visible: receiving > 0 || sending > 0

            Rectangle {
                color:   Qt.rgba(255, 255, 255, 0.1)
                width:   1
                height:  45

            }

            BeamAmount {
                Layout.leftMargin: 20
                amount:            sending
                color:             Style.accent_outgoing
                lightFont:         false
                fontSize:          16
                currencySymbol:    Utils.symbolBeam
                copyMenuEnabled:   true
                //% "Sending"
                caption:           qsTrId("available-panel-sending")
                showZero:          false
                prefix:            "-"
            }

            Item {
                Layout.fillWidth: true
            }

            BeamAmount {
                amount:            receiving
                color:             Style.accent_incoming
                lightFont:         false
                fontSize:          16
                currencySymbol:    Utils.symbolBeam
                copyMenuEnabled:   true
                //% "Receiving"
                caption:           qsTrId("available-panel-receiving")
                showZero:          false
                prefix:            "+"
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }
}
