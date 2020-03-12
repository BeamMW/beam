package main

import (
	"github.com/SherClockHolmes/webpush-go"
	"time"
)

func sendPushNotification (data string, endpoint string, key string, auth string, expires int64) error {
	sub := webpush.Subscription{}
	sub.Endpoint    = endpoint
	sub.Keys.P256dh = key
	sub.Keys.Auth   = auth

	var ttl = int(expires - time.Now().Unix())
	resp, err := webpush.SendNotification([]byte(data), &sub, &webpush.Options{
		Subscriber:      config.PushContactMail,
		TTL:             ttl,
		VAPIDPublicKey:  config.VAPIDPublic,
		VAPIDPrivateKey: config.VAPIDPrivate,
	})

	if resp != nil {
		defer resp.Body.Close()
	}

	return err
}
