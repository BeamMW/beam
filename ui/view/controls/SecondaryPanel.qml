import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import "."

Rectangle {
    property string title
    property string value
    property color amountColor

    radius: 10
    color: Style.dark_slate_blue

    clip: true

    SFText {
        font {
            pixelSize: 18
            weight: Font.Bold
        }

        color: Style.white

        x: 30
        y: 30
        text: title
    }

    Row {
        x: 30
        y: 88
        spacing: 6

        SFText {
            font {
                weight: Font.ExtraLight
                pixelSize: 36
            }

            color: amountColor

            text: value

            anchors.bottom: parent.bottom
        }

        SFText {
            font {
                pixelSize: 24
                weight: Font.ExtraLight
            }

            color: amountColor

            text: "BEAM"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 3
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
}
