package main

import (
	"github.com/SherClockHolmes/webpush-go"
	"net/http"
	"time"
)

func sendPushNotification (data string, endpoint string, key string, auth string, expires int64) (*http.Response, error) {
	sub := webpush.Subscription{}
	sub.Endpoint    = endpoint + "2a2"
	sub.Keys.P256dh = key
	sub.Keys.Auth   = auth

	var ttl = int(expires - time.Now().Unix())
	resp, err := webpush.SendNotification([]byte(data), &sub, &webpush.Options{
		Subscriber:      config.PushContactMail,
		TTL:             ttl,
		VAPIDPublicKey:  config.VAPIDPublic,
		VAPIDPrivateKey: config.VAPIDPrivate,
	})

	return resp, err
}
