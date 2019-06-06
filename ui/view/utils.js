function formatDateTime(datetime, localeName) {
    var maxTime = new Date(4294967295000);
    if (datetime >= maxTime) {
        //: time never string
        //% "never"
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
