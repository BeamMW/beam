self.addEventListener('push', function (event) {
    console.log("push event: ", JSON.stringify(event))
    console.log("event.data", JSON.stringify(event.data))
    console.log("event.data.text()", event.data.text())

    const sendNotification = body => {
        const title = "New Transaction";
        return self.registration.showNotification(title, {
            body,
        });
    };

    if (event.data) {
        const json = JSON.parse(event.data.text())
        if (json.txtype !== "simple") return; // now it is always simple

        let message = ""
        if (json.failureReason !== undefined) {
            if (json.failureReason === 1) {
                message = ["Transaction", json.txid, "cancelled"].join(" ")
            } else {
                message = ["Transaction", json.txid, "failed with code", json.failureReason].join(" ")
            }
        } else {
            message = ["You have new transaction for address", json.address, "amount is", json.amount, "Groth"].join(" ")
        }

        event.waitUntil(sendNotification(message));
    }
})

self.addEventListener('pushsubscriptionchange', function(event) {
    console.log('push expired');
    // seems to be not implemented in chrome
    // unsubscribe event.oldSubscription
    // subscribe event.newSubscription
})
