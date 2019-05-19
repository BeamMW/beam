import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import "."

ComboBox {
    id: control
    
    property string color
	textRole: "walletID"
    spacing: 4

    delegate: ItemDelegate {
        id: itemDelegate
        width: control.width
        contentItem: SFText {
            text: "<b>" + modelData.name+"</b> (" + modelData.walletID +")"
            color: control.color
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
			font.pixelSize: 14
        }
        highlighted: control.highlightedIndex === index

        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 40
            opacity: enabled ? 1 : 0.3
            color:itemDelegate.highlighted ? Style.content_secondary : Style.accent_incoming
        }
    }

    indicator: SvgImage {
        source: "qrc:/assets/icon-down.svg"
        anchors.right: control.right
        anchors.verticalCenter: control.verticalCenter
    }

    contentItem: SFTextInput {
        leftPadding: 0
        rightPadding: control.indicator.width + control.spacing
        clip: true
        text: control.editText
        color: control.color
		font.pixelSize: 14
        verticalAlignment: Text.AlignVCenter
        validator: control.validator
    }

    background: Item {
        Rectangle {
            width: control.width
            height: 1
            y: control.height - 1
            color: Style.separator
        }
    }

    popup: Popup {
        y: control.height - 1
        width: control.width
        padding: 1

        contentItem: ColumnLayout {
            /*SFText {
                Layout.fillWidth: true
                Layout.minimumHeight: control.height
                Layout.leftMargin: 20
                color: Style.content_disabled
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter

                //% "create new address"
                text: qsTrId("create-new-address")
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Style.separator
            }*/
            ListView {
                id: listView
                Layout.fillWidth: true
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }            
            }
            Item {
                Layout.fillWidth: true
                Layout.minimumHeight:15
            }
        }

        background: Item {
            Rectangle {
                color: Style.accent_incoming
                anchors.left: parent.left
                anchors.right: parent.right
                height: control.height
            }
            Rectangle {
                anchors.fill: parent
                color: Style.accent_incoming
                radius: 10
            }
        }
    }
}
