export default class webpush {
    worker
    serverKey
    subscription
    connection

    get userSubscribed() {
        return !!this.subscription
    }

    get blockedByUser() {
        return Notification.permission === 'denied'
    }

    constructor() {
        this.serverKey    = "BKV4kJcvOcY3CiDFntZQilujWbdZ0NWzyDjBIrY7y8JVVGrOuoyCu8xroxRueh0-3_W-tF5kryJ0M4VAEKj2kAE"
        this.subscription = undefined
        this.worker       = undefined
        this.connection   = undefined
    }

    async register(connection) {
        if (!connection) throw Error("No server connection provided")
        this.connection = connection

        if (this.blockedByUser) throw Error("Notifications are blocked by user")

        if (!this.worker) {
            this.worker = await navigator.serviceWorker.register('worker.js')
        }

        this.subscription = await this.worker.pushManager.getSubscription()
        console.log('webpush subscription:', this.subscription)
        return this.userSubscribed
    }

    async subscribe() {
        if (this.blockedByUser) throw Error("Notifications are blocked by user")
        this.subscription = await this.worker.pushManager.subscribe({
            userVisibleOnly: true,
            applicationServerKey: this.serverKey,
        })
        console.log('webpush subscription:', this.subscription)
        return this.userSubscribed
    }

    async unsubscribe() {
    }

    async notifyServer(connection, params) {
        return new Promise((resolve, reject) => {
            let onclose = () => {
                cleanup()
				reject(new Error("connection closed"))
			}

			let onerror = error => {
                cleanup()
                reject(error)
            }

            let cleanup = () => {
                connection.removeEventListener('onclose', onclose)
                connection.removeEventListener('onerror', onerror)
            }

            connection.addEventListener('onclose', onclose)
			connection.addEventListener('onerror', onerror)

            let subscriptionParams = Object.assign({}, params)
            subscriptionParams.NotificationEndpoint = this.subscription.endpoint
            subscriptionParams.ServerKey = this.subscription.options.applicationServerKey

            console.log("Subscription params", JSON.stringify(subscriptionParams))
            connection.send(JSON.stringify({
                jsonrpc: "2.0",
                id: 666,
                method: "subscribe",
                params: subscriptionParams
            }))

            connection.onmessage = e => {
                let data = JSON.parse(e.data)
                if (data.result) {
                    console.log(`subscribe: result is ${JSON.stringify(data.result)}`)
                    if (data.id === 666) {
                        console.log("Subscribe OK. STARTING...")
                        cleanup()
                        resolve()
                    }
                } else {
                    console.log("Subscription error,", JSON.stringify(data))
                }
            }
        })
    }
}

/*
function encodeKey(rawKey) {
    const padding = '='.repeat((4 - rawKey.length % 4) % 4);
    const base64 = (rawKey + padding)
    .replace(/-/g, '+')
    .replace(/_/g, '/')

    const rawData = window.atob(base64);
    const outputArray = new Uint8Array(rawData.length)

    for (let i = 0; i < rawData.length; ++i) {
        outputArray[i] = rawData.charCodeAt(i)
    }
    return outputArray
}
*/