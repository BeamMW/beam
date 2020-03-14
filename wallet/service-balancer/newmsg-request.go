package main

import (
	"beam.mw/service-balancer/wsclient"
	"encoding/json"
	"log"
)

type rpcNewmsgParams struct {
	Address string `json:"address"`
}

type rpcNewmsgResult struct {
	Handled bool `json:"handled"`
}

func onNewBbsMessage(client *wsclient.WSClient, params *json.RawMessage) (result interface{}, err error) {
	var nmParams rpcNewmsgParams
	var nmResult rpcNewmsgResult

	if err = json.Unmarshal(*params, &nmParams); err != nil {
		return
	}
	if config.Debug {
		log.Printf("new bbs message request, address %v", nmParams.Address)
	}

	go func() {
		err := forEachSub(nmParams.Address, func(sub *Subscription) {
			go func () {
				resp, err := sendPushNotification(string(*params), sub.NotificationEndpoint, sub.P256dhKey, sub.AuthKey, sub.ExpiresAt)
				if err != nil {
					log.Printf("sendPushNotification %v", err)
				}
				if resp != nil {
					defer resp.Body.Close()
					// This usually means that endpoint doesn't exist anymore
					// Let's stop listening and sending notifications for this endpoint
					if resp.StatusCode == 404 {
						if config.Debug {
							log.Printf("404 on push notification %v:%v", sub.SbbsAddress, sub.NotificationEndpoint)
						}
						unsub := UnsubParams{
							SbbsAddress:          sub.SbbsAddress,
							SbbsAddressPrivate:   sub.SbbsAddressPrivate,
							NotificationEndpoint: sub.NotificationEndpoint,
						}
						if err := monitorUnsubscribe(&unsub); err != nil {
							log.Printf("monitorUnsubscribe error %v", err)
						}
					}
				}
			}()
		})
		if err != nil {
			log.Printf("forEachSub error %v", err)
		}
	} ()

	nmResult.Handled = true
	result = &nmResult
	return
}
