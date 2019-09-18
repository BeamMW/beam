import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import Beam.Wallet 1.0
import "."
import "../utils.js" as Utils

Control {
    id: control

    property string title

    property double beamReceiving
    property double beamSending
    property double beamLocked

    leftPadding:   25
    rightPadding:  25
    topPadding:    25
    bottomPadding: 25

    background: Rectangle {
        radius:  10
        color:   Style.background_second
    }

    function calcColSpan(curr, valBtc, valLtc, valQtum) {
        if (curr == Currency.CurrBtc)  return valLtc && !valQtum ? 2 : 1
        if (curr == Currency.CurrLtc)  return valQtum && !valBtc ? 2 : 1
        if (curr == Currency.CurrQtum) return !valLtc && !valBtc ? 2 : 1
        return 1
    }


    Component {
        id: columnTitle
        SFText {
            Layout.alignment:     Qt.AlignHCenter
            font.pixelSize:       12
            font.styleName:       "Normal"
            font.weight:          Font.Bold
            color:                Style.content_main
            font.capitalization:  Font.AllUppercase
            opacity:              0.6
            text:                 titleText
        }
    }

    Component {
        id: amountText
        SFLabel {
            font.styleName:  "Light"
            font.weight:     Font.Normal
            font.pixelSize:  20
            color:           amountColor
            text:            [amount == 0 ? "" : signSymbol, amount == 0 ? "0" : Utils.formatAmount(amount), " ", currSymbol].join("")
            copyMenuEnabled: true
            onCopyText:      BeamGlobals.copyToClipboard(amount)
        }
    }

    contentItem: ColumnLayout {
        SFText {
            Layout.bottomMargin: 5
            font.pixelSize:      18
            font.styleName:      "Bold"
            font.weight:         Font.Bold
            color:               Style.content_main
            text:                title
        }

        Item {Layout.fillHeight: true}

        RowLayout {
            Item {Layout.fillWidth: true}
            GridLayout {
                Layout.fillWidth: true
                columns:       3
                columnSpacing: 50

                // Receiving
                ColumnLayout {
                    spacing: 10
                    Layout.alignment: Qt.AlignTop

                    Loader {
                        Layout.alignment: Qt.AlignHCenter
                        sourceComponent: columnTitle
                        property string titleText: qsTrId("general-receiving")
                    }

                    GridLayout {
                        Layout.alignment: Qt.AlignHCenter
                        columnSpacing: 10
                        rowSpacing:    10
                        columns:       2
                        rows:          2
                        flow:          GridLayout.TopToBottom

                        Loader {
                            Layout.alignment:   Qt.AlignHCenter
                            sourceComponent:    amountText
                            property double     amount:       control.beamReceiving
                            property string     amountColor:  Style.accent_incoming
                            property string     signSymbol:   "+"
                            property string     currSymbol:   Utils.symbolBeam
                        }

                    }

                    SvgImage {
                        Layout.alignment: Qt.AlignHCenter
                        sourceSize:       Qt.size(30, 30)
                        source:           "qrc:/assets/icon-received.svg"
                    }
                }

                // Sending
                ColumnLayout {
                    spacing: 10
                    Layout.alignment: Qt.AlignTop

                    Loader {
                        Layout.alignment: Qt.AlignHCenter
                        sourceComponent:  columnTitle
                        property string  titleText: qsTrId("general-sending")
                    }

                    GridLayout {
                        Layout.alignment: Qt.AlignHCenter
                        columnSpacing: 10
                        rowSpacing:    10
                        columns:       2
                        rows:          2
                        flow:          GridLayout.TopToBottom

                        Loader {
                            Layout.alignment: Qt.AlignHCenter
                            sourceComponent:  amountText
                            property double   amount:      control.beamSending
                            property string   amountColor: Style.accent_outgoing
                            property string   signSymbol:  "-"
                            property string   currSymbol:  Utils.symbolBeam
                        }

                    }

                    SvgImage {
                        Layout.alignment: Qt.AlignHCenter
                        sourceSize:       Qt.size(30, 30)
                        source:           "qrc:/assets/icon-sent.svg"
                    }
                }

                // Locked
                ColumnLayout {
                    spacing: 10
                    Layout.alignment: Qt.AlignTop

                    Loader {
                        Layout.alignment: Qt.AlignHCenter
                        sourceComponent:  columnTitle
                        //% "locked"
                        property string   titleText: qsTrId("general-locked")
                    }

                    GridLayout {
                        Layout.alignment: Qt.AlignHCenter
                        columnSpacing: 10
                        rowSpacing:    10
                        columns:       2
                        rows:          2
                        flow:          GridLayout.TopToBottom

                        Loader {
                            Layout.alignment: Qt.AlignHCenter
                            sourceComponent:  amountText
                            property double   amount:      control.beamLocked
                            property string   amountColor: Style.content_main
                            property string   signSymbol:  ""
                            property string   currSymbol:  Utils.symbolBeam
                        }

                    }

                    SvgImage {
                        Layout.alignment: Qt.AlignHCenter
                        sourceSize:       Qt.size(30, 30)
                        source:           "qrc:/assets/icon-padlock.svg"
                    }
                }
            }
            Item {Layout.fillWidth: true}
        }

        Item {Layout.fillHeight: true}
    }
}
