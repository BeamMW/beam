package main

import (
	"encoding/json"
	"log"
)

type rpcNewmsgParams struct {
	Address string `json:"address"`
	Amount  uint64 `json:"amount"`
}

type rpcNewmsgResult struct {
	Handled bool `json:"handled"`
}

func onNewBbsMessage(client *WSClient, params *json.RawMessage) (result interface{}, err error) {
	var nmParams rpcNewmsgParams
	var nmResult rpcNewmsgResult

	if err = json.Unmarshal(*params, &nmParams); err != nil {
		return
	}
	log.Printf("new bbs message request, address %v", nmParams.Address)

	go func() {
		err := forEachSub(nmParams.Address, func(sub *subEntry) {
			go func () {
				err := sendPushNotification(string(*params), sub.NotificationEndpoint, sub.P256dhKey, sub.AuthKey)
				if err != nil {
					log.Printf("push notification error %v", err)
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
