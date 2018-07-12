import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import "."

Rectangle {
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

            text: walletViewModel.available

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
        text: walletViewModel.available + " USD"
    }
}
