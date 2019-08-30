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

    property double btcReceiving
    property double btcSending
    property double btcLocked

    property double ltcReceiving
    property double ltcSending
    property double ltcLocked

    property double qtumReceiving
    property double qtumSending
    property double qtumLocked

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

                        Loader {
                            Layout.alignment:  Qt.AlignHCenter
                            Layout.columnSpan: calcColSpan(Currency.CurrBtc, control.btcReceiving, control.ltcReceiving, control.qtumReceiving)
                            visible: control.btcReceiving > 0
                            sourceComponent:   amountText
                            property double    amount:       control.btcReceiving
                            property string    amountColor:  Style.accent_incoming
                            property string    signSymbol:   "+"
                            property string    currSymbol:   Utils.symbolBtc
                        }

                        Loader {
                            Layout.alignment:  Qt.AlignHCenter
                            Layout.columnSpan: calcColSpan(Currency.CurrLtc, control.btcReceiving, control.ltcReceiving, control.qtumReceiving)
                            visible: control.ltcReceiving > 0
                            sourceComponent:   amountText
                            property double    amount:       control.ltcReceiving
                            property string    amountColor:  Style.accent_incoming
                            property string    signSymbol:   "+"
                            property string    currSymbol:   Utils.symbolLtc
                        }

                        Loader {
                            Layout.alignment:  Qt.AlignHCenter
                            Layout.columnSpan: calcColSpan(Currency.CurrQtum, control.btcReceiving, control.ltcReceiving, control.qtumReceiving)
                            visible: control.qtumReceiving > 0
                            sourceComponent:   amountText
                            property double    amount:       control.qtumReceiving
                            property string    amountColor:  Style.accent_incoming
                            property string    signSymbol:   "+"
                            property string    currSymbol:   Utils.symbolQtum
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

                        Loader {
                            Layout.alignment:  Qt.AlignHCenter
                            Layout.columnSpan: 1//calcColSpan(Currency.CurrBtc, SecondaryPanel.ColType.ColSending)
                            visible: control.btcSending > 0
                            sourceComponent:   amountText
                            property double    amount:       control.btcSending
                            property string    amountColor:  Style.accent_outgoing
                            property string    signSymbol:   "-"
                            property string    currSymbol:   Utils.symbolBtc
                        }

                        Loader {
                            Layout.alignment:  Qt.AlignHCenter
                            Layout.columnSpan: 1//calcColSpan(Currency.CurrLtc, SecondaryPanel.ColType.ColSending)
                            visible: control.ltcSending > 0
                            sourceComponent:   amountText
                            property double    amount:       control.ltcSending
                            property string    amountColor:  Style.accent_outgoing
                            property string    signSymbol:   "-"
                            property string    currSymbol:   Utils.symbolLtc
                        }

                        Loader {
                            Layout.alignment:  Qt.AlignHCenter
                            Layout.columnSpan: 1//calcColSpan(Currency.CurrQtum, SecondaryPanel.ColType.ColSending)
                            visible: control.qtumSending > 0
                            sourceComponent:   amountText
                            property double    amount:       control.qtumSending
                            property string    amountColor:  Style.accent_outgoing
                            property string    signSymbol:   "-"
                            property string    currSymbol:   Utils.symbolQtum
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

                        Loader {
                            Layout.alignment:  Qt.AlignHCenter
                            Layout.columnSpan: 1//calcColSpan(Currency.CurrBtc, SecondaryPanel.ColType.ColLocked)
                            visible: control.btcLocked > 0
                            sourceComponent:   amountText
                            property double    amount:       control.btcLocked
                            property string    amountColor:  Style.content_main
                            property string    signSymbol:   ""
                            property string    currSymbol:   Utils.symbolBtc
                        }

                        Loader {
                            Layout.alignment:  Qt.AlignHCenter
                            Layout.columnSpan: 1//calcColSpan(Currency.CurrLtc, SecondaryPanel.ColType.ColLocked)
                            visible: control.ltcLocked > 0
                            sourceComponent:   amountText
                            property double    amount:       control.ltcLocked
                            property string    amountColor:  Style.content_main
                            property string    signSymbol:   ""
                            property string    currSymbol:   Utils.symbolLtc
                        }

                        Loader {
                            Layout.alignment:  Qt.AlignHCenter
                            Layout.columnSpan: 1//calcColSpan(Currency.CurrQtum, SecondaryPanel.ColType.ColLocked)
                            visible: control.qtumLocked > 0
                            sourceComponent:   amountText
                            property double    amount:       control.qtumLocked
                            property string    amountColor:  Style.content_main
                            property string    signSymbol:   ""
                            property string    currSymbol:   Utils.symbolQtum
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
