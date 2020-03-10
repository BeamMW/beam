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

// @arg amount - any number or float string in "C" locale
function uiStringToLocale (amount) {
    var locale = Qt.locale();
    var parts  = amount.toString().split(".");

    // Trim leading zeros
    var intPart = parseInt(parts[0], 10);
    var left = isNaN(intPart) ? parts[0] : intPart.toString();

    left = left.replace(/(\d)(?=(?:\d{3})+\b)/g, "$1" + locale.groupSeparator);
    return parts[1] ? [left, parts[1]].join(locale.decimalPoint) : left;
}

function localeDecimalToCString(amount) {
    var locale = Qt.locale();
    return amount
        .split(locale.groupSeparator)
        .join('')
        .split(locale.decimalPoint)
        .join('.');
}

function getLogoTopGapSize(parentHeight) {
    return parentHeight * (parentHeight < 768 ? 0.13 : 0.18)
}

function openExternal(externalLink, settings, dialog, onFinish) {
    var onFinishCallback = onFinish && (typeof onFinish === "function")
        ? onFinish
        : function () {};
    if (settings.isAllowedBeamMWLinks) {
        Qt.openUrlExternally(externalLink);
        onFinishCallback();
    } else {
        dialog.externalUrl = externalLink;
        dialog.onOkClicked = function () {
            settings.isAllowedBeamMWLinks = true;
            onFinishCallback();
        };
        dialog.onCancelClicked = function() {
            onFinishCallback();
        };
        dialog.open();
    }
}

function navigateToDownloads() {
    var settingsViewModel = Qt.createQmlObject("import Beam.Wallet 1.0; SettingsViewModel {}", main);
    var component = Qt.createComponent("controls/UpdateAppFromSiteConfirmation.qml");
    var externalLinkConfirmation = component.createObject(main);
    Utils.openExternal(
        "https://www.beam.mw/#downloads",
        settingsViewModel,
        externalLinkConfirmation);
}

function currenciesList() {
    return ["BEAM", "BTC", "D", "LTC", "QTUM"]
}

// TODO: refact all currencies labels to one place
const symbolBeam  = "BEAM";
const symbolBtc   = "BTC";
const symbolLtc   = "LTC";
const symbolD     = "D";
const symbolQtum  = "QTUM";

const maxAmount   = "254000000";
const minAmount   = "0.00000001";
