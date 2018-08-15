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
            text: modelData.name+"(" + modelData.walletID +")"
            color: control.color
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
        }
        highlighted: control.highlightedIndex === index

        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 40
            opacity: enabled ? 1 : 0.3
            color:itemDelegate.highlighted ? Style.bluey_grey : Style.combobox_color
        }
    }

    indicator: SvgImage {
        source: "qrc:///assets/icon-down.svg"
        anchors.right: control.right
        anchors.verticalCenter: control.verticalCenter
    }

    contentItem: SFTextInput {
        leftPadding: 0
        rightPadding: control.indicator.width + control.spacing
        clip: true
        text: control.editText
        color: control.color
        verticalAlignment: Text.AlignVCenter
    }

    background: Item {
        Rectangle {
            width: control.width
            height: 1
            y: control.height - 1
            color: Style.separator_color
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
                color: Style.disable_text_color
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter

                text: qsTr("create new address")
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Style.separator_color
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
                color: Style.combobox_color
                anchors.left: parent.left
                anchors.right: parent.right
                height: control.height
            }
            Rectangle {
                anchors.fill: parent
                color: Style.combobox_color
                radius: 10
            }
        }
    }
}
