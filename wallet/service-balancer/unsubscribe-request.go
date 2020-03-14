package main

import (
	"encoding/json"
	"errors"
	"github.com/olahol/melody"
	"log"
)

type UnsubParams struct {
	SbbsAddress          string `json:"SbbsAddress"`
	SbbsAddressPrivate   string `json:"SbbsAddressPrivate"`
	NotificationEndpoint string `json:"NotificationEndpoint"`
}

type UnsubResult struct {
	Unsubscribe bool  `json:"unsubscribe"`
}

func onWalletUnsubscribe(session* melody.Session, params *json.RawMessage) (result interface{}, err error) {
	wid, err := getValidWID(session)
	if err != nil {
		return
	}

	var unsub UnsubParams
	if err = json.Unmarshal(*params, &unsub); err != nil {
		return
	}

	if config.Debug {
		log.Printf("wallet %v, rpc unsubscribe request %v:%v", wid, unsub.SbbsAddress, unsub.NotificationEndpoint)
	}

	if len(unsub.SbbsAddress) == 0 {
		return nil, errors.New("invalid SBBS address")
	}

	if len(unsub.SbbsAddressPrivate) == 0 {
		return nil, errors.New("invalid SBBS key")
	}

	if !checkBbsKeys(unsub.SbbsAddress, unsub.SbbsAddressPrivate) {
		return nil, errors.New("keys mismatch")
	}

	if len(unsub.NotificationEndpoint) == 0 {
		return nil, errors.New("invalid notification endpoint")
	}

	if err = monitorUnsubscribe(&unsub); err != nil {
		return
	}

	result = &UnsubResult{
		Unsubscribe: true,
	}

	return
}

