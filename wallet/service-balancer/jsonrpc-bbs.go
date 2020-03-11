package main

import (
	"encoding/json"
	"fmt"
)

func jsonRpcProcessBbs (client *WSClient, msg []byte) (response []byte) {
	return jsonRpcProcess(msg, func(method string, params *json.RawMessage) (errCode int, err error, result interface{}) {
		if method == "new_message" {
			result, err = onNewBbsMessage(client, params)
			return
		}
		err = fmt.Errorf("method '%v' not found", method)
		errCode = -32601
		return
	})
}
