import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import "."

Rectangle {

    id: wallet_layout
    anchors.fill: parent
    color: Style.marine
    state: "wide"

    // Component.onCompleted:{
    //     console.log(FontLoader.Ready)
    // }

    Item {
        y: 97
        height: 206

        anchors.left: parent.left
        anchors.right: parent.right

        Row {

            id: wide_panels

            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.height

            spacing: 30

            AvailablePanel {
                width: (parent.width - 3*30)*500/1220
                height: parent.height                
            }

            Rectangle {
                width: (parent.width - 3*30)*240/1220
                height: parent.height
                radius: 10
                color: Style.dark_slate_blue
            }

            Rectangle {
                width: (parent.width - 3*30)*240/1220
                height: parent.height
                radius: 10
                color: Style.dark_slate_blue
            }

            Rectangle {
                width: (parent.width - 3*30)*240/1220
                height: parent.height
                radius: 10
                color: Style.dark_slate_blue
            }
            
        }

        Row {

            id: medium_panels

            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.height

            spacing: 30

            AvailablePanel {
                width: (parent.width - parent.spacing)*518/864
                height: parent.height              
            }

            Rectangle {
                width: (parent.width - parent.spacing)*346/864
                height: parent.height
                radius: 10
                color: Style.dark_slate_blue
            }
            
            visible: false
        }
    }
    
    states: [
        State {
            name: "wide"
        },

        State {
            when: wallet_layout.width < (1440-70-2*30)
            name: "medium"
            PropertyChanges {target: wide_panels; visible: false}
            PropertyChanges {target: medium_panels; visible: true}
        },

        State {
            name: "small"
        }
    ]
}
