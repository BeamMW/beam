package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
)

const (
	RpcKeyWID = "wid"
)

type rpcHeader struct {
	Jsonrpc string           `json:"jsonrpc"`
	Id      *json.RawMessage `json:"id"`
	Result  *json.RawMessage `json:"result"`
	Params  *json.RawMessage `json:"params"`
	Error   *json.RawMessage `json:"error"`
	Method  string           `json:"method"`
}

type rpcRequest struct {
	Jsonrpc string           `json:"jsonrpc"`
	Id      *json.RawMessage `json:"id"`
	Method  string           `json:"method"`
	Params  *json.RawMessage `json:"params"`
}

type rpcResponse struct {
	Jsonrpc string           `json:"jsonrpc"`
	Id      *json.RawMessage `json:"id"`
	Result  *json.RawMessage `json:"result"`
}

/* Not used at the moment, but may be would be used in the future
type rpcError struct {
	Code int `json:"code"`
	Message string `json:"message"`
}
*/

func getIdStr(rawid *json.RawMessage) string {
	if rawid == nil {
		return ""
	}
	return string(*rawid)
}

type fnRpcMethod func(string, *json.RawMessage) (int, error, interface{})

func jsonRpcProcess(msg []byte, handler fnRpcMethod) (response []byte) {
	var err error
	var errCode int

	var requestId  *json.RawMessage
	var requestResult interface{}

	defer func () {
		if err == nil {
			if requestResult != nil {
				var resp = rpcResponse{
					Jsonrpc: "2.0",
					Id:      requestId,
				}
				rawres, err := json.Marshal(requestResult)
				if err == nil {
					rawmsg := json.RawMessage(rawres)
					resp.Result = &rawmsg
					response, err = json.Marshal(resp)
				}
			}
		}
		if err != nil {
			var errFmt = `{"jsonrpc":"2.0", "id": "-1", "error": {"code": %v, "message": "%v"}}`
			var rpcError = fmt.Sprintf(errFmt, errCode, err.Error())
			log.Printf("jsonrpc error: %v", rpcError)
			response = []byte(rpcError)
		}
	} ()

	var header = rpcHeader{}
	if err := json.Unmarshal(msg, &header); err != nil {
		errCode = -32700
		return
	}

	if header.Error != nil {
		log.Printf("jsonrpc, received error response for id [%v], result %v", getIdStr(header.Id), string(*header.Error))
		return
	}

	if header.Result != nil {
		if config.Debug {
			log.Printf("jsonrpc, received response for id [%v], result %v", getIdStr(header.Id), string(*header.Result))
		}
		return
	}

	//
	// Assume we're dealing with request (method call)
	//
	if requestId = header.Id; header.Id == nil {
		errCode = -32600
		err = fmt.Errorf("missing jsonrpc id")
		return
	}

	if header.Jsonrpc != "2.0" {
		errCode = -32600
		err = fmt.Errorf("bad jsonrpc version %v", header.Jsonrpc)
		return
	}

	if header.Params == nil {
		err = errors.New("bad jsonrpc, params are nil")
		errCode = -32600
		return
	}

	if len(header.Method) == 0 {
		err = errors.New("bad jsonrpc, empty method")
		errCode = -32600
		return
	}

	errCode, err, requestResult = handler(header.Method, header.Params)
	if err == nil {
		if requestResult == nil {
			errCode = -32600
			err = fmt.Errorf("jsonrpc, empty result returned from %v handler", header.Method)
		}
	}

	return
}

