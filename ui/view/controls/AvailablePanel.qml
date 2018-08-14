import QtQuick 2.4
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import "."

Rectangle {
	id: panel
    radius: 10
    color: Style.dark_slate_blue

    clip: true

    property string value
    property alias model : utxo_list.model
	property alias color: panel.color

    SFText {
        id: title
        font {
            pixelSize: 18
            weight: Font.Bold
        }

        color: Style.white

        x: 30
        y: 30
        text: qsTr("Available")
    }

    Row
    {
        x: 30
        y: 61
        spacing: 6

        SFText {
            font.pixelSize: 64
            font.weight: Font.ExtraLight
            color: Style.bright_teal

            text: value

            anchors.bottom: parent.bottom
        }

        SFText {
            font.pixelSize: 36
            font.weight: Font.ExtraLight
            color: Style.bright_teal

            text: "BEAM"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
        }
    }

    SFText {
        font {
            weight: Font.ExtraLight
            pixelSize: 24
        }

        color: Style.bluey_grey

        x: 30
        y: 147
        text: value + " USD"
    }

    SFText {
		id: linkBtn

        anchors {
            left: title.right
            bottom: title.bottom
            leftMargin: 10
        }

        font.pixelSize: 18
        color: Style.bluey_grey
        linkColor: Style.white

        text: "<a href=\"utxo\">utxo</a>"

        onLinkActivated: {
            if(link == "utxo")
			{
                utxo_popup.visible = true;
                utxo_popup.open();
			}
		}

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    Popup {
        id: utxo_popup
        parent: Overlay.overlay
        background: null
        visible: false

        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 400
        height:300
        dim: true
        clip: true
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        Rectangle
        {
            radius: 10
            color: Style.dark_slate_blue

            anchors.fill: parent


            clip: true
            ListView {
                id: utxo_list
                anchors {
                    fill: parent
                    margins: 30
                }


                delegate: ItemDelegate {

                    RowLayout {
                        width: parent.width
                        SFText {
                            text: '<b>Amount:</b> ' + modelData.amount
                            color: Style.white
                        }
                        SFText {
                            text: '<b>Maturuty:</b> ' + modelData.maturity
                            color: Style.white
                        }
                        SFText {
                            text: '<b>Type:</b> ' + modelData.type
                            color: Style.white
                        }
                    }
                }
            }
        }
        onClosed: {
            visible = false;
        }
    }
}
