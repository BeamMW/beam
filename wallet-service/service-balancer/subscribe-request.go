package main

import (
	"encoding/json"
	"errors"
	"github.com/olahol/melody"
	"log"
	"net/url"
	"time"
)

type SubParams struct {
	SbbsAddress          string `json:"SbbsAddress"`
	SbbsAddressPrivate   string `json:"SbbsAddressPrivate"`
	NotificationEndpoint string `json:"NotificationEndpoint"`
	ServerKey            string `json:"ServerKey"`
	P256dhKey            string `json:"P256dhKey"`
	AuthKey              string `json:"AuthKey"`
	ExpiresAt            int64  `json:"ExpiresAt"`
}

type SubResult struct {
	Subscribe bool  `json:"subscribe"`
}

func onWalletSubscribe(session* melody.Session, params *json.RawMessage) (result interface{}, err error) {
	wid, err := getValidWID(session)
	if err != nil {
		return
	}

	var subscribeParams SubParams
	if err = json.Unmarshal(*params, &subscribeParams); err != nil {
		return
	}

	if config.Debug {
		log.Printf("wallet %v, rpc subscribe request, address %v, expires %v", wid, subscribeParams.SbbsAddress, time.Unix(subscribeParams.ExpiresAt, 0))
	}

	if len(subscribeParams.SbbsAddress) == 0 {
		return nil, errors.New("invalid SBBS address")
	}

	if len(subscribeParams.SbbsAddressPrivate) == 0 {
		return nil, errors.New("invalid SBBS key")
	}

	if !checkBbsKeys(subscribeParams.SbbsAddress, subscribeParams.SbbsAddressPrivate) {
		return nil, errors.New("keys mismatch")
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

	if err = monitorSubscribe(&subscribeParams); err != nil {
		return
	}

	result = &SubResult{
		Subscribe: true,
	}

	return
}
