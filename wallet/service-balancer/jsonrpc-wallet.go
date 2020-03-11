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
				result, err = onWalletLogin(session, params)
				return
			}

			if method == "logout" {
				result, err = onWalletLogout(session, params)
				return
			}

			if method == "subscribe" {
				result, err = rpcSubscribeRequest(session, params)
				return
			}

			err = fmt.Errorf("method '%v' not found", method)
			errCode = -32601
			return
		})
}

