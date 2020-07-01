package main

import (
	"encoding/json"
	"fmt"
	"github.com/olahol/melody"
)

func jsonRpcProcessWallet (session *melody.Session, msg []byte) (response []byte) {
	return jsonRpcProcess(msg,
		func(method string, params *json.RawMessage) (errCode int, err error, result interface{}) {
			if method == "login" {
				counters.CountLogin()
				result, err = onWalletLogin(session, params)
				return
			}

			if method == "logout" {
				counters.CountLogout()
				result, err = onWalletLogout(session, params)
				return
			}

			if method == "subscribe" {
				counters.CountSubscribe()
				result, err = onWalletSubscribe(session, params)
				return
			}

			if method == "unsubscribe" {
				counters.CountUnsubscribe()
				result, err = onWalletUnsubscribe(session, params)
				return
			}

			counters.CountWBadMethod()
			err = fmt.Errorf("method '%v' not found", method)
			errCode = -32601
			return
		})
}

