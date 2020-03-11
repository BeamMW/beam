self.addEventListener('push', function (event) {
    console.log("push event: ", JSON.stringify(event))
    console.log("event.data", JSON.stringify(event.data))
    console.log("event.data.text()", JSON.stringify(event.data.text()))
 /*   if (!(self.Notification && self.Notification.permission === 'granted')) {
        console.log("Notifications disabled")
        return;
    }

    const sendNotification = body => {
        // you could refresh a notification badge here with postMessage API
        const title = "Web Push example";

        return self.registration.showNotification(title, {
            body,
        });
    };

    if (event.data) {
        console.log
        const message = event.data.text();
        event.waitUntil(sendNotification(message));
    }*/
});