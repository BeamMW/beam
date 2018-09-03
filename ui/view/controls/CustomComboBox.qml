import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import "."

ComboBox {
    id: control
    
    spacing: 4

    textRole: "text"

    delegate: ItemDelegate {
        id: itemDelegate
        width: control.width
        contentItem: SFText {
            text: model.text
            color: Style.white
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
			font.pixelSize: 12
        }

        highlighted: control.highlightedIndex === index

        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 20
            opacity: enabled ? 1 : 0.3
            color:itemDelegate.highlighted ? Style.bluey_grey : Style.combobox_color
        }
    }

    indicator: SvgImage {
        source: "qrc:///assets/icon-down.svg"
        anchors.right: control.right
        anchors.verticalCenter: control.verticalCenter
    }

    contentItem: SFText {
        leftPadding: 0
        rightPadding: control.indicator.width + control.spacing
        clip: true
        text: control.currentText
        color: Style.white
		font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
    }

    background: Item {
        Rectangle {
            width: control.width
            height: control.activeFocus || control.hovered ? 2 : 1
            y: control.height - 1
            color: Style.white
            opacity: (control.activeFocus || control.hovered)? 0.3 : 0.1
        }
    }

    popup: Popup {
        y: control.height - 1
        width: control.width
        padding: 1

        contentItem: ColumnLayout {
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
                Layout.minimumHeight:10
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
