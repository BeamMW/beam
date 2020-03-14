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
				err := sendPushNotification(string(*params), sub.NotificationEndpoint, sub.P256dhKey, sub.AuthKey, sub.ExpiresAt)
				if err != nil {
					// TODO: check when to remove bad endpoint
					log.Printf("send push notification error %v", err)
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
