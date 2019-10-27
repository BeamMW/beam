import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import Beam.Wallet 1.0
import "."
import "../utils.js" as Utils

Control {
    id:            control
    leftPadding:   25
    rightPadding:  25
    topPadding:    20
    bottomPadding: 20

    //
    // Common props
    //
    property alias  title:                    controlTitle.text
    property alias  showSeedDialogTitle:      seedPhraseDialog.showSeedDialogTitle
    property alias  showAddressesDialogTitle: showAddressesDialog.showAddressesDialogTitle
    property string feeRateLabel:        ""
    property int    minFeeRate:          0
    property string color:               Qt.rgba(Style.content_main.r, Style.content_main.g, Style.content_main.b, 0.5)
    property alias  editElectrum:        useElectrumSwitch.checked
    property bool   canEdit:             true

    property bool isConnected: false
    property bool isNodeConnection: false
    property bool isElectrumConnection: false

    //
    // Node props
    //
    property alias  address:      addressInput.address
    property alias  username:     usernameInput.text
    property alias  password:     passwordInput.text
    property alias  feeRate:      feeRateInput.fee
    property bool   canEditNode:  !control.isNodeConnection

    //
    // Electrum props
    //
    property alias addressElectrum:            addressInputElectrum.address
    property alias seedPhrasesElectrum:        seedPhraseDialog.seedPhrasesElectrum
    property alias phrasesSeparatorElectrum:   seedPhraseDialog.phrasesSeparatorElectrum
    property bool  isCurrentElectrumSeedValid: false
    property bool  canEditElectrum:            !control.isElectrumConnection

    // function to get "receiving" addresses
    property var   getAddressesElectrum:       undefined

    //
    // signals
    //
    signal disconnect
    // node
    signal applyNode
    signal clearNode
    signal connectToNode
    // electrum
    signal newSeedElectrum
    signal restoreSeedElectrum
    signal applyElectrum
    signal clearElectrum
    signal connectToElectrum
    signal copySeedElectrum
    signal validateCurrentSeedPhrase

    QtObject {
        id: internalCommon
        property int    initialFeeRate
        function restore() {
            feeRate  = initialFeeRate
        }

        function save() {
            initialFeeRate  = feeRate
        }

        function isChanged() {
            return initialFeeRate !== feeRate
        }
    }

    QtObject {
        id: internalNode
        property string initialAddress
        property string initialUsername
        property string initialPassword        

        function restore() {
            address  = initialAddress
            username = initialUsername
            password = initialPassword
        }

        function save() {
            initialAddress  = address
            initialUsername = username
            initialPassword = password
        }

        function isChanged() {
            return initialAddress  !== address
                || initialUsername !== username
                || initialPassword !== password
        }
    }

    function isSettingsChanged() {
        return  internalCommon.isChanged() || (editElectrum ? internalElectrum.isChanged() : internalNode.isChanged());
    }

    function canApplySettings() {
        return editElectrum ? canApplyElectrum() : canApplyNode();
    }

    function applyChanges() {
        internalCommon.save();
        return editElectrum ? applyChangesElectrum() : applyChangesNode();
    }

    function restoreSettings() {
        internalCommon.restore();
        return editElectrum ? internalElectrum.restore() : internalNode.restore();
    }

    function haveSettings() {
        return editElectrum ? haveElectrumSettings() : haveNodeSettings();
    }

    function clear() {
        internalCommon.save();
        if (editElectrum) {
            control.clearElectrum();
            internalElectrum.save();
        }
        else {
            control.clearNode();
            internalNode.save();
        }
    }

    function canClear() {
        return control.canEdit && (editElectrum ? canClearElectrum() : canClearNode());
    }

    function canDisconnect() {
        return control.canEdit && isConnected && (editElectrum ? canDisconnectElectrum() : canDisconnectNode());
    }

    function canApplyNode() {
        return feeRate >= minFeeRate && password.length && username.length && addressInput.isValid
    }

    function applyChangesNode() {
        internalNode.save()
        control.applyNode()
    }

    function canClearNode() {
        return !isNodeConnection && (internalNode.initialPassword.length || internalNode.initialUsername.length || internalNode.initialAddress.length);
    }

    function canDisconnectNode() {
        return isNodeConnection;
    }

    function haveNodeSettings() {
        return feeRate >= minFeeRate && password.length && username.length && addressInput.isValid;
    }

    //
    // Electrum props
    //

    QtObject {
        id: internalElectrum
        property string initialAddress
        property string initialSeed

        function restore() {
            // TODO: change
            addressElectrum = initialAddress
            control.restoreSeedElectrum()
        }

        function save() {
            initialAddress  = addressElectrum
        }

        function isChanged() {
            // TODO: change
            return initialAddress !== addressElectrum
               // || initialSeed !== seedElectrum
        }
    }

    function canApplyElectrum() {
        return feeRate >= minFeeRate && isCurrentElectrumSeedValid && addressInputElectrum.isValid
    }

    function canClearElectrum() {
        return !isElectrumConnection && (internalElectrum.initialAddress.length || internalElectrum.initialSeed.length);
    }

    function canDisconnectElectrum() {
        return isElectrumConnection;
    }

    function applyChangesElectrum() {
        internalElectrum.save();
        control.applyElectrum();
    }

    function haveElectrumSettings() {
        // TODO: fix
        return feeRate >= minFeeRate && /*isCurrentElectrumSeedValid &&*/ addressInputElectrum.isValid;
    }

    Component.onCompleted: {
        control.editElectrum = control.isElectrumConnection;
        internalCommon.save();
        internalNode.save();
        internalElectrum.save();
    }

    background: Rectangle {
        radius:  10
        color:   Style.background_second
    }

    contentItem: ColumnLayout {
        RowLayout {
            width: parent.width

            // TODO: indicator

            SFText {
                id:                  controlTitle
                color:               control.color
                font.pixelSize:      14
                font.weight:         Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing:  3.11
            }
        }

        ColumnLayout {
            id: editableLayout
            Layout.fillHeight: true
            Layout.fillWidth:  true

            // Node & Electrum switch
            RowLayout {
                Layout.topMargin: 18
                Layout.bottomMargin: 18
                spacing: 10

                SFText {
                    //% "Node"
                    text:  qsTrId("settings-swap-node")
                    color: useElectrumSwitch.checked ? control.color : Style.active
                    font.pixelSize: 14
                }

                CustomSwitch {
                    id:          useElectrumSwitch
                    alwaysGreen: true
                    spacing:     0
                }

                SFText {
                    //% "Electrum"
                    text: qsTrId("general-electrum")
                    color: useElectrumSwitch.checked ? Style.active : control.color
                    font.pixelSize: 14
                }

                Item {
                    Layout.fillWidth: true
                }

                LinkButton {
                    Layout.alignment: Qt.AlignVCenter
                    linkStyle: "<style>a:link {color: '#f9605b'; text-decoration: none;}</style>"
                    //% "Clear"
                    text:       qsTrId("settings-reset")
                    visible:    canClear()
                    onClicked:  clear()
                }

                LinkButton {
                    Layout.alignment: Qt.AlignVCenter
                    linkStyle: "<style>a:link {color: '#f9605b'; text-decoration: none;}</style>"
                    //% "Disconnect"
                    text:       qsTrId("settings-swap-disconnect")
                    visible:    canDisconnect()
                    onClicked:  disconnect()
                }
            }

            GridLayout {
                columns:          2
                columnSpacing:    30
                rowSpacing:       15

                SFText {
                    visible:        !editElectrum
                    font.pixelSize: 14
                    color:          control.color
                    //% "Node Address"
                    text:           qsTrId("settings-node-address")
                }

                IPAddrInput {
                    id:               addressInput
                    visible:          !editElectrum
                    Layout.fillWidth: true
                    color:            Style.content_main
                    underlineVisible: canEditNode
                    readOnly:         !canEditNode
                }

                SFText {
                    visible:        !editElectrum
                    font.pixelSize: 14
                    color:          control.color
                    //% "Username"
                    text:           qsTrId("settings-username")
                }

                SFTextInput {
                    id:               usernameInput
                    visible:          !editElectrum
                    Layout.fillWidth: true
                    font.pixelSize:   14
                    color:            Style.content_main
                    activeFocusOnTab: true
                    underlineVisible: canEditNode
                    readOnly:         !canEditNode
                }

                SFText {
                    visible:        !editElectrum
                    font.pixelSize: 14
                    color:          control.color
                    //% "Password"
                    text:           qsTrId("settings-password")
                }

                SFTextInput {
                    id:               passwordInput
                    visible:          !editElectrum
                    Layout.fillWidth: true
                    font.pixelSize:   14
                    color:            Style.content_main
                    activeFocusOnTab: true
                    echoMode:         TextInput.Password
                    underlineVisible: canEditNode
                    readOnly:         !canEditNode
                }

                // electrum settings
                SFText {
                    visible:        editElectrum
                    font.pixelSize: 14
                    color:          control.color
                    //% "Node Address"
                    text:           qsTrId("settings-node-address")
                }

                IPAddrInput {
                    visible:          editElectrum
                    id:               addressInputElectrum
                    Layout.fillWidth: true
                    color:            Style.content_main
                    ipOnly:           false
                    underlineVisible: canEditElectrum
                    readOnly:         !canEditElectrum
                }

                // common fee rate
                SFText {
                    font.pixelSize: 14
                    color:          control.color
                    //% "Default fee"
                    text:           qsTrId("settings-fee-rate")
                }

                FeeInput {
                    id:                  feeRateInput
                    Layout.fillWidth:    true
                    fillWidth:           true
                    inputPreferredWidth: -1
                    minFee:              control.minFeeRate
                    feeLabel:            control.feeRateLabel
                    color:               Style.content_main
                    spacing:             0
                    underlineVisible:    canEdit
                    readOnly:            !canEdit
                }
            }

            // electrum settings - seed: new || edit
            RowLayout {
                visible:             editElectrum && canEditElectrum
                spacing:             20
                Layout.fillWidth:    true
                Layout.topMargin:    30
                Layout.bottomMargin: control.canEdit ? 37 : 15

                LinkButton {
                    text:      isCurrentElectrumSeedValid ?   
                               //% "Edit your seed phrase"
                               qsTrId("settings-swap-edit-seed") :
                               //% "Enter your seed phrase"
                               qsTrId("settings-swap-enter-seed")
                    onClicked: {
                        seedPhraseDialog.setModeEdit()
                        seedPhraseDialog.isCurrentElectrumSeedValid = isCurrentElectrumSeedValid
                        seedPhraseDialog.open()
                    }
                }

                SFText {
                    font.pixelSize: 14
                    color:          control.color
                    //% "or"
                    text:           qsTrId("settings-swap-label-or")
                }

                LinkButton {
                    //% "Generate new seed phrase"
                    text:             qsTrId("settings-swap-new-seed")
                    onClicked: {
                        newSeedElectrum();
                        seedPhraseDialog.setModeNew();
                        seedPhraseDialog.open();
                    }
                }
            }

            // electrum settings: show seed && show addresses
            RowLayout {
                visible:             editElectrum && !canEditElectrum
                spacing:             20
                Layout.fillWidth:    true
                Layout.topMargin:    30
                Layout.bottomMargin: control.canEdit ? 37 : 15

                LinkButton {
                    //% "Show seed phrase"
                    text:      qsTrId("settings-swap-show-seed")
                    onClicked: {
                        seedPhraseDialog.setModeView();
                        seedPhraseDialog.open();
                    }
                }

                LinkButton {
                    //% "Show wallet addresses"
                    text:      qsTrId("settings-swap-show-addresses")
                    onClicked: {
                        showAddressesDialog.addressesElectrum = getAddressesElectrum();
                        showAddressesDialog.open();
                    }
                }
            }

            // alert text if we have active transactions
            SFText {
                visible:               !control.canEdit
                Layout.preferredWidth: 500
                horizontalAlignment:   Text.AlignHCenter
                verticalAlignment:     Text.AlignVCenter
                font.pixelSize:        14
                wrapMode:              Text.WordWrap
                color:                 control.color
                lineHeight:            1.1 
                //% "You cannot disconnect wallet, edit seed phrase or change default fee while you have\ntransactions in progress. Please wait untill transactions are completed\nand try again."
                text:                  qsTrId("settings-progress-na")
            }

            // buttons
            // "cancel" "apply"
            // "connect to node" or "connect to electrum"
            RowLayout {
                visible:          control.canEdit
                Layout.fillWidth: true
                Layout.topMargin: 25
                spacing:          15

                Item {
                    Layout.fillWidth: true
                }

                CustomButton {
                    visible:                !connectButtonId.visible
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  130
                    leftPadding:  25
                    rightPadding: 25
                    text:         qsTrId("general-cancel")
                    icon.source:  "qrc:/assets/icon-cancel-white.svg"
                    enabled:      isSettingsChanged()
                    onClicked:    restoreSettings()
                }

                PrimaryButton {
                    visible:                !connectButtonId.visible
                    leftPadding:            25
                    rightPadding:           25
                    text:                   qsTrId("settings-apply")
                    icon.source:            "qrc:/assets/icon-done.svg"
                    enabled:                isSettingsChanged() && canApplySettings()
                    onClicked:              applyChanges()
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  130
                }

                PrimaryButton {
                    id:                     connectButtonId
                    visible:                !isSettingsChanged() && haveSettings() && (editElectrum ? !isElectrumConnection : !isNodeConnection)
                    leftPadding:            25
                    rightPadding:           25
                    text:                   editElectrum ?
                                            //% "connect to electrum node"
                                            qsTrId("settings-swap-connect-electrum") :
                                            //% "connect to node"
                                            qsTrId("settings-swap-connect-node");
                    icon.source:            "qrc:/assets/icon-connect.svg"
                    onClicked:              editElectrum ? connectToElectrum() : connectToNode();
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  editElectrum ? 250 : 195
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }
    }

    Dialog {
        id: seedPhraseDialog

        width:       800
        height:      430
        parent:      Overlay.overlay
        x:           Math.round((parent.width - width) / 2)
        y:           Math.round((parent.height - height) / 2)
        closePolicy: Popup.NoAutoClose
        modal:       true
        visible:     false
        
        property string showSeedDialogTitle:        ""
        property string phrasesSeparatorElectrum:   ""
        property var    seedPhrasesElectrum:        undefined
        property bool   isCurrentElectrumSeedValid: false

        signal newSeedElectrum
        signal copySeedElectrum
        signal undoChanges
        signal validateFullSeedPhrase

        onNewSeedElectrum: control.newSeedElectrum()
        onCopySeedElectrum: control.copySeedElectrum()
        onUndoChanges: {
            for(var i = 0; i < seedPhraseDialog.seedPhrasesElectrum.length; ++i)
            {
                seedPhraseDialog.seedPhrasesElectrum[i].revertChanges();
            }
            validateFullSeedPhrase();
        }

        onValidateFullSeedPhrase: control.validateCurrentSeedPhrase()

        function setModeEdit() {
            seedDialogContent.state = "editPhrase";
        }

        function setModeView() {
            seedDialogContent.state = "viewPhrase";
        }

        function setModeNew() {
            seedDialogContent.state = "newPhrase";
        }

        function applySeedPhrase() {
            for(var i = 0; i < seedPhraseDialog.seedPhrasesElectrum.length; ++i)
            {
                seedPhraseDialog.seedPhrasesElectrum[i].applyChanges();
            }
        }

        background: Rectangle {
            radius:       10
            color:        Style.background_popup
            anchors.fill: parent
        }

        contentItem: 
        ColumnLayout {
            id: seedDialogContent
            anchors.fill:          parent
            anchors.margins:       30
            spacing:               0
            property bool canEdit: false
            state:                 "newPhrase"

            // Title: New seed phrase / Enter your seed phrase / "coin" seed phrase
            SFText {
                id: seedDialogTitle
                Layout.fillWidth:    true
                color:               Style.white
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize:      18
                font.weight:         Font.Bold
            }

            // additional comment (only on "New seed phrase"):
            SFText {
                id: additionalInfo
                Layout.topMargin:    14
                Layout.fillWidth:    true
                visible:             false
                color:               Style.white
                wrapMode:            Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize:      14
                font.weight:         Font.Normal
                //% "Your seed phrase is the access key to all the funds! Print or write down the phrase and keep it in a safe or in a locked vault.\nWithout the phrase you will not be able to recover your money."
                text: qsTrId("swap-seed-info-message")
            }
            
            // body: seed phrase
            GridLayout {
                Layout.topMargin:    50
                Layout.bottomMargin: 50
                columns:             4
                columnSpacing:       30
                rowSpacing:          20
                                    
                Repeater {
                    model: seedPhrasesElectrum
                    Rectangle {
                        id:           phraseItem
                        border.color: seedDialogContent.canEdit ? "transparent" : Style.background_second
                        color:        "transparent"
                        width:        160
                        height:       38
                        radius:       30
            
                        RowLayout {
                            spacing:      0
                            anchors.fill: parent
            
                            // index
                            Rectangle {
                                Layout.leftMargin: 9

                                color:  Style.background_second
                                width:  20
                                height: 20
                                radius: 10
            
                                SFText {
                                    anchors.centerIn: parent
                                    text:             modelData.index + 1
                                    font.pixelSize:   10
                                    color:            Style.content_main
                                }
                            }
                        
                            // inpunt
                            SFTextInput {
                                id: phraseValue
                                visible:               seedDialogContent.canEdit
                                Layout.leftMargin:     10
                                Layout.preferredWidth: 110

                                font.pixelSize:   14
                                color:            (modelData.isAllowed || modelData.value.length == 0) ? Style.content_main : Style.validator_error
                                backgroundColor:  (modelData.isAllowed || modelData.value.length == 0) ? Style.content_main : Style.validator_error
                                text:             modelData.value

                                onTextEdited: {
                                    var phrases = text.split(phrasesSeparatorElectrum);
                                    if (phrases.length >= seedPhraseDialog.seedPhrasesElectrum.length) {
                                        for(var i = 0; i < seedPhraseDialog.seedPhrasesElectrum.length; ++i)
                                        {
                                            seedPhraseDialog.seedPhrasesElectrum[i].value = phrases[i];
                                        }
                                    }
                                    seedPhraseDialog.validateFullSeedPhrase();
                                }
                                Binding {
                                    target:   modelData
                                    property: "value"
                                    value:    phraseValue.text
                                }
                            }

                            SFText {
                                visible:               !seedDialogContent.canEdit
                                Layout.leftMargin:     10
                                Layout.preferredWidth: 110
                                horizontalAlignment:   Text.AlignLeft
                                text:                  modelData.phrase
                                font.pixelSize:        14
                                color:                 Style.content_main
                            }
                        }
                    }
                }
            }

            // buttons
            RowLayout {
                spacing:                20
                Layout.fillWidth:       true
                Layout.preferredHeight: 38

                Item {
                    Layout.fillWidth: true
                }

                // editPhrase: "cancel" "apply"
                CustomButton {
                    id: cancelButtonId
                    visible:                false
                    Layout.preferredHeight: 38
                    Layout.minimumWidth:    133
                    text:                   qsTrId("general-cancel")
                    icon.source:            "qrc:/assets/icon-cancel-white.svg"
                    enabled:                true
                    onClicked: {
                        seedPhraseDialog.undoChanges();
                        seedPhraseDialog.close();
                    }
                }

                PrimaryButton {
                    id: applyButtonId
                    visible:                false
                    Layout.preferredHeight: 38
                    Layout.minimumWidth:    126
                    text:                   qsTrId("settings-apply")
                    icon.source:            "qrc:/assets/icon-done.svg"
                    // TODO: add checking isSeedChanged
                    enabled: {
                        var enable = seedPhraseDialog.isCurrentElectrumSeedValid;
                        for (var i = 0; i < seedPhraseDialog.seedPhrasesElectrum.length; ++i) {
                            enable &= seedPhraseDialog.seedPhrasesElectrum[i].isAllowed;
                        }
                        return enable;
                    }
                    onClicked: {
                        seedPhraseDialog.applySeedPhrase();
                        seedPhraseDialog.close();
                    }
                }

                // viewPhrase: "close" "copy"
                // newPhrase:  "close" "generate another seed phrase" "copy"
                CustomButton {
                    id: closeButtonId
                    visible:                false
                    Layout.preferredHeight: 38
                    Layout.minimumWidth:    125
                    text:                   qsTrId("general-close")
                    icon.source:            "qrc:/assets/icon-cancel-white.svg"
                }

                CustomButton {
                    id: generateButtonId
                    visible:                false
                    Layout.preferredHeight: 38
                    Layout.minimumWidth:    271
                    rightPadding:           20
                    //% "generate another seed phrase"
                    text:                   qsTrId("settings-swap-seed-generate")
                    icon.source:            "qrc:/assets/icon-repeat-white.svg"
                    onClicked:              seedPhraseDialog.newSeedElectrum();
                }

                PrimaryButton {
                    id: copyButtonId
                    visible:                false
                    Layout.preferredHeight: 38
                    Layout.minimumWidth:    124
                    text:                   qsTrId("general-copy")
                    icon.source:            "qrc:/assets/icon-copy-blue.svg"
                    onClicked:              seedPhraseDialog.copySeedElectrum();
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            states: [
                State {
                    name: "newPhrase"
                    PropertyChanges {
                        target: seedDialogTitle
                        //% "New seed phrase"
                        text: qsTrId("swap-seed-new")
                    }
                    PropertyChanges {
                        target: additionalInfo
                        visible: true
                    }
                    PropertyChanges {
                        target: closeButtonId
                        visible: true
                        onClicked: {
                            seedPhraseDialog.close()
                        }
                    }
                    PropertyChanges {
                        target: generateButtonId
                        visible: true
                    }
                    PropertyChanges {
                        target: copyButtonId
                        visible: true
                    }
                },
                State {
                    name: "editPhrase"
                    PropertyChanges {
                        target: seedDialogTitle
                        //% "Enter your seed phrase"
                        text: qsTrId("swap-seed-edit")
                    }
                    PropertyChanges {
                        target: seedPhraseDialog
                        height: 380
                    }
                    PropertyChanges {
                        target: seedDialogContent
                        canEdit: true
                    }
                    PropertyChanges {
                        target: cancelButtonId
                        visible: true
                    }
                    PropertyChanges {
                        target: applyButtonId
                        visible: true
                    }
                },
                State {
                    name: "viewPhrase"
                    PropertyChanges {
                        target: seedDialogTitle
                        text: showSeedDialogTitle
                    }
                    PropertyChanges {
                        target: seedPhraseDialog
                        height: 380
                    }
                    PropertyChanges {
                        target: closeButtonId
                        visible: true
                        onClicked: seedPhraseDialog.close()
                    }
                    PropertyChanges {
                        target: copyButtonId
                        visible: true
                    }
                }
            ]
        }
    }

    Dialog {
        id: showAddressesDialog

        width:   460
        height:  400
        parent:  Overlay.overlay
        x:       Math.round((parent.width - width) / 2)
        y:       Math.round((parent.height - height) / 2)
        modal:   true

        property alias showAddressesDialogTitle: showAddressesDialogTitleId.text
        property var   addressesElectrum: undefined

        background: Rectangle {
            radius:       10
            color:        Style.background_popup
            anchors.fill: parent
        }

        contentItem: ColumnLayout {
            spacing: 0
            anchors.fill:    parent
            anchors.margins: 30

            // title
            SFText {
                id: showAddressesDialogTitleId
                Layout.fillWidth:     true
                color:                Style.white
                horizontalAlignment:  Text.AlignHCenter
                font.pixelSize:       18
                font.weight:          Font.Bold
            }

            // body
            ScrollView {
                Layout.fillWidth:          true
                Layout.fillHeight:         true
                Layout.topMargin:          50
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                clip:                      true

                ColumnLayout {
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                    spacing:           30

                    Repeater {
                        model: showAddressesDialog.addressesElectrum
                        SFLabel {
                            Layout.fillWidth:    true
                            horizontalAlignment: Text.AlignLeft
                            text:                modelData
                            font.pixelSize:      14
                            color:               Style.content_main
                            copyMenuEnabled:     true
                            onCopyText:          BeamGlobals.copyToClipboard(text)
                        }
                    }
                }
            }

            // buttons
            CustomButton {
                Layout.topMargin:       24
                Layout.alignment:       Qt.AlignHCenter
                Layout.preferredHeight: 38
                Layout.preferredWidth:  125
                text:             qsTrId("general-close")
                icon.source:      "qrc:/assets/icon-cancel-white.svg"
                onClicked:        showAddressesDialog.close()
            }
        }
    }
}