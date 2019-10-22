import Beam.Wallet 1.0

ConfirmationDialog {
    TokenBootstrapManager {
        id: instance
    }
    property alias model: instance
    property var checkTokenForDuplicate: function(token) {
        model.checkTokenForDuplicate(token);
    }
    width:                  460
    //% "Оffer is already accepted"
    title:                  qsTrId("swap-offer-duplicate-title")
    //% "The offer with this transaction token is already accepted.\nPlease check the swap token and try again."
    text:                   qsTrId("swap-offer-duplicate-message")
    //% "ok"
    okButtonText:           qsTrId("swap-offer-duplicate-confirm-button")
    okButtonIconSource:     "qrc:/assets/icon-done.svg"
    cancelButtonVisible:    false
}
