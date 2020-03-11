package main

import (
	"github.com/SherClockHolmes/webpush-go"
)

func sendPushNotification (data string, endpoint string, key string, auth string) error {
	sub := webpush.Subscription{}
	sub.Endpoint    = endpoint
	sub.Keys.P256dh = key
	sub.Keys.Auth   = auth

	resp, err := webpush.SendNotification([]byte(data), &sub, &webpush.Options{
		Subscriber:      config.PushContactMail,
		// TODO: set real TTL
		TTL:             30,
		VAPIDPublicKey:  config.VAPIDPublic,
		VAPIDPrivateKey: config.VAPIDPrivate,
	})

	if resp != nil {
		defer resp.Body.Close()
	}

	return err
}
