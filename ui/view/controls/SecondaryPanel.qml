import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.3
import "."

Rectangle {
    id: panel
    property string title
    property string receiving
    property string sending
    property string maturing

    radius: 10
    color: Style.dark_slate_blue
    clip: true

    signal copyValueText(string value)

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 30
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        anchors.bottomMargin: 36
        spacing: 30

        SFText {
            id: title_id
            font.pixelSize: 18
            font.styleName: "Bold";
            font.weight: Font.Bold
            color: Style.white
            text: title
        }
                        
        RowLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 10

            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 10

                SFText {
                    Layout.alignment: Qt.AlignHCenter
                    font.pixelSize: 12
                    font.styleName: "Normal";
                    font.weight: Font.Bold
                    color: Style.white
                    opacity: 0.6
                    text: qsTr("RECEIVING")
                }

                SFLabel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.styleName: "Light"
                    font.weight: Font.Normal
                    font.pixelSize: 20
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 14
                    color: Style.bright_sky_blue
                    text: (receiving !== "0") ? "+" + receiving : receiving;
                    elide: Text.ElideRight
                    copyMenuEnabled: true
                    onCopyText: panel.copyValueText(receiving)
                }

                SvgImage {
                    Layout.alignment: Qt.AlignHCenter
                    sourceSize: Qt.size(30, 30)
                    source: "qrc:/assets/icon-received.svg"
                }
            }

            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 10

                SFText {
                    Layout.alignment: Qt.AlignHCenter
                    font.pixelSize: 12
                    font.styleName: "Normal";
                    font.weight: Font.Bold
                    color: Style.white
                    opacity: 0.6
                    text: qsTr("SENDING")
                }

                SFLabel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.styleName: "Light"
                    font.weight: Font.Normal
                    font.pixelSize: 20
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 14
                    color: Style.heliotrope
                    text: (sending !== "0") ? "-" + sending : sending;
                    elide: Text.ElideRight
                    copyMenuEnabled: true
                    onCopyText: panel.copyValueText(sending)
                }

                SvgImage {
                    Layout.alignment: Qt.AlignHCenter
                    sourceSize: Qt.size(30, 30)
                    source: "qrc:/assets/icon-sent.svg"
                }
            }

            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 10

                SFText {
                    Layout.alignment: Qt.AlignHCenter
                    font.pixelSize: 12
                    font.styleName: "Normal";
                    font.weight: Font.Bold
                    color: Style.white
                    opacity: 0.6
                    text: qsTr("MATURING")
                }

                SFLabel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.styleName: "Light"
                    font.weight: Font.Normal
                    font.pixelSize: 20
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 14
                    color: Style.white
                    text: maturing
                    elide: Text.ElideRight
                    copyMenuEnabled: true
                    onCopyText: panel.copyValueText(maturing)
                }

                SvgImage {
                    Layout.alignment: Qt.AlignHCenter
                    sourceSize: Qt.size(30, 30)
                    source: "qrc:/assets/icon-maturing.svg"
                }
            }
        }
    }
}
