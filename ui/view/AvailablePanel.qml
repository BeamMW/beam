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

    SFText {
        font {
            pixelSize: 64
        }

        color: Style.bright_teal

        x: 30
        y: 61
        text: walletViewModel.available
    }

    SFText {
        font {
            pixelSize: 24
        }

        color: Style.bluey_grey

        x: 30
        y: 147
        text: qsTr("0000.0 USD")
    }
}