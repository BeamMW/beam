import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.4
import Beam.Wallet 1.0
import "."

Component
{
    ColumnLayout
    {
        anchors.fill:parent
        spacing: 0
        Image
        {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: true
            Layout.preferredWidth: 242
            Layout.preferredHeight: 170
            Layout.maximumHeight: 170
            Layout.minimumHeight: 120

            fillMode: Image.PreserveAspectFit

            source: "qrc:/assets/start-logo.svg"
        }

        SFText 
        {
            Layout.alignment: Qt.AlignHCenter

            //% "BEAM"
            text: qsTrId("logo-name")
            color: Style.accent_incoming
            font.pixelSize: 32
            font.styleName: "Bold"; font.weight: Font.Bold
            font.letterSpacing: 20
            leftPadding: 20
        }

        Item
        {
            Layout.fillHeight: true
            Layout.minimumHeight: 30
            Layout.maximumHeight: 40
        }

        SFText
        {
            Layout.alignment: Qt.AlignHCenter

            //% "Scalable confidential cryptocurrency"
            text: qsTrId("logo-description")

            color: Style.accent_incoming
            font.pixelSize: 18
            font.styleName: "Bold"; font.weight: Font.Bold
        }

        Item
        {
            id: stagingLabelAligner
            visible: false
            Layout.fillHeight: true
            Layout.minimumHeight: 30
            Layout.maximumHeight: 40
        }
        SFText
        {
            id: stagingLabel
            visible: false
            Layout.alignment: Qt.AlignHCenter
            color: Style.content_secondary
            font.pixelSize: 18
            font.styleName: "Bold"; font.weight: Font.Bold
        }

        Component.onCompleted: {
            var themeName = Theme.name();
            if (themeName != "mainnet") {
                stagingLabelAligner.visible = true;
                stagingLabel.text = themeName.toUpperCase();
                stagingLabel.visible = true;
            }
        }
    }
}