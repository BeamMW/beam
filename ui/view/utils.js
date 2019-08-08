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

function formatAmount (amount, toPlainNumber) {
    return amount ? amount.toLocaleString(toPlainNumber ? Qt.locale("C") : Qt.locale(), 'f', -128) : ""
}

function getLogoTopGapSize(parentHeight) {
    return parentHeight * (parentHeight < 768 ? 0.13 : 0.18)
}

function calcDisplayRate(ail, air) {
    // ai[X] = amount input control
    var cl = ail.currency
    var cr = air.currency
    if (cl == cr) return 1;

    var al = ail.amount
    var ar = air.amount
    if (al == 0 || ar == 0) return "?"

    var rounder = 100000000
    return Math.round(ar / al * rounder) / rounder
}

const symbolBeam = '\u{EAFB}'