import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.4
import Beam.Wallet 1.0
import "."

ColumnLayout
{
    id: lll
    function themeName() {
        return Theme.name();
    }
    function isMainNet() {
        return themeName() == "mainnet";
    }
    spacing: 0

    Item {
        Layout.preferredHeight: 60
        visible: isMainNet()
    }

    SvgImage
    {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 242
        Layout.preferredHeight: 208
        source: "qrc:/assets/start-logo.svg"
    }

    SFText
    {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 40
        Layout.preferredHeight: 20

        //% "Scalable confidential cryptocurrency"
        text: qsTrId("logo-description")

        color: Style.accent_incoming
        font.pixelSize: 18
        font.styleName: "Bold"; font.weight: Font.Bold
    }

    SFText
    {
        visible: !isMainNet()
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredHeight: 20
        Layout.topMargin: 40
        color: Style.content_secondary
        text: themeName()
        font.pixelSize: 18
        font.styleName: "Bold"; font.weight: Font.Bold
        font.capitalization: Font.AllUppercase
    }

    Item {
        Layout.preferredHeight: 30 
    }
}
