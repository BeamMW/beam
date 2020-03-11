package main

import (
	"encoding/json"
	"errors"
	"github.com/olahol/melody"
	"log"
	"net/url"
)

type rpcSubscribeParams struct {
	SbbsAddress          string `json:"SbbsAddress"`
	SbbsAddressPrivate   string `json:"SbbsAddressPrivate"`
	NotificationEndpoint string `json:"NotificationEndpoint"`
	ServerKey            string `json:"ServerKey"`
	P256dhKey            string `json:"P256dhKey"`
	AuthKey              string `json:"AuthKey"`
}

type rpcSubscribeResult struct {
	Subscribe bool  `json:"subscribe"`
}

func rpcSubscribeRequest(session* melody.Session, params *json.RawMessage) (result interface{}, err error) {
	wid, err := getValidWID(session)
	if err != nil {
		return
	}

	log.Printf("wallet %v, rpc subscribe request", wid)

	var subscribeParams rpcSubscribeParams
	if err = json.Unmarshal(*params, &subscribeParams); err != nil {
		return
	}

	if len(subscribeParams.SbbsAddress) == 0 {
		return nil, errors.New("invalid SBBS address")
	}

	if len(subscribeParams.SbbsAddressPrivate) == 0 {
		return nil, errors.New("invalid SBBS key")
	}

	if len(subscribeParams.NotificationEndpoint) == 0 {
		return nil, errors.New("invalid notification endpoint")
	}

	if _, err = url.ParseRequestURI(subscribeParams.NotificationEndpoint); err != nil {
		return
	}

	if subscribeParams.ServerKey != config.VAPIDPublic {
		return nil, errors.New("server key mismatch")
	}

	if len(subscribeParams.P256dhKey) == 0 {
		return nil, errors.New("invalid P256dhKey")
	}

	if len(subscribeParams.AuthKey) == 0 {
		return nil, errors.New("invalid AuthKey")
	}

	monitorSubscribe(&subscribeParams)
	result = &rpcSubscribeResult{
		Subscribe: true,
	}

	return
}
