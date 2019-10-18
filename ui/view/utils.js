function formatDateTime(datetime, localeName) {
    var maxTime = new Date(4294967295000);
    if (datetime >= maxTime) {
        //: time never string
        //% "Never"
        return qsTrId("time-never");
    }
    var timeZoneShort = datetime.getTimezoneOffset() / 60 * (-1);
    return datetime.toLocaleDateString(localeName)
         + " | "
         + datetime.toLocaleTimeString(localeName)
         + (timeZoneShort >= 0 ? " (GMT +" : " (GMT ")
         + timeZoneShort
         + ")";
}

function formatAmount (amount, toPlainNumber, showZero) {
    return amount ? amount.toLocaleString(toPlainNumber ? Qt.locale("C") : Qt.locale(), 'f', -128) : (showZero ? "0" : "")
}

function getLogoTopGapSize(parentHeight) {
    return parentHeight * (parentHeight < 768 ? 0.13 : 0.18)
}

function handleMousePointer(mouse, element) {
    element.cursorShape = element.parent.linkAt(mouse.x, mouse.y).length
        ? element.cursorShape = Qt.PointingHandCursor
        : element.cursorShape = Qt.ArrowCursor;
}

function openExternal(externalLink, settings, dialog) {
    if (settings.isAllowedBeamMWLinks) {
        Qt.openUrlExternally(externalLink);
    } else {
        dialog.externalUrl = externalLink;
        dialog.onOkClicked = function () {
            settings.isAllowedBeamMWLinks = true;
        };
        dialog.open();
    }
}

function handleExternalLink(mouse, element, settings, dialog) {
    if (element.cursorShape == Qt.PointingHandCursor) {
        var externalLink = element.parent.linkAt(mouse.x, mouse.y);
        if (settings.isAllowedBeamMWLinks) {
            Qt.openUrlExternally(externalLink);
        } else {
            dialog.externalUrl = externalLink;
            dialog.onOkClicked = function () {
                settings.isAllowedBeamMWLinks = true;
            };
            dialog.open();
        }
        return true;
    }
}

function calcDisplayRate(aiReceive, aiSend, numOnly) {
    // ai[X] = amount input control
    var cr = aiReceive.currency
    var cs = aiSend.currency
    if (cr == cs) return 1

    var ams = aiSend.amount
    var amr = aiReceive.amount
    if (ams == 0 || amr == 0) return ""

    return (ams / amr).toLocaleString(numOnly ? Qt.locale("C") : Qt.locale(), 'f', 28).replace(/\.?0+$/,"")
    return (amr / ams).toLocaleString(numOnly ? Qt.locale("C") : Qt.locale(), 'f', 28).replace(/\.?0+$/,"")
}

function getAmountWithoutCurrency(amountWithCurrency) {
    return amountWithCurrency.split(" ")[0];
}

function currenciesList() {
    return ["BEAM", "BTC", "LTC", "QTUM"]
}

const symbolBeam  = '\uEAFB'
const symbolBtc   = '\u20BF'
const symbolLtc   = '\u0141'
const symbolQtum  = '\uEAFD'