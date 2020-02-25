package main

import (
	"encoding/json"
	"errors"
	"github.com/olahol/melody"
	"log"
	"net/http"
	"reflect"
	"runtime"
)

// TODO: allow CORs only for domains from config
func allowCORS(w http.ResponseWriter, r *http.Request) {
	
	methods := r.Header.Get("Access-Control-Request-Method")
	if len(methods) != 0 {
		w.Header().Add("Access-Control-Allow-Methods", methods)
	}

	headers := r.Header.Get("Access-Control-Request-Headers")
	if len(headers) != 0 {
		w.Header().Add("Access-Control-Allow-Headers", headers)
	}

	origin := r.Header.Get("Origin")
	if len(origin) == 0 {
		origin = "*"
	}
	w.Header().Add("Access-Control-Allow-Origin", origin)
}

type genericR struct {
	Error string  `json:"error,omitempty"`
}

func wrapHandler(handler func(w http.ResponseWriter, r *http.Request)(interface{}, error))http.HandlerFunc{
	var fname = runtime.FuncForPC(reflect.ValueOf(handler).Pointer()).Name()
	return func (w http.ResponseWriter, r *http.Request) {
		allowCORS(w, r)

		if r.Method == "OPTIONS" {
			return
		}

		var res interface{} = nil
		res, err := handler(w, r)

		if err != nil {
			res = genericR{
				Error: err.Error(),
			}
			w.WriteHeader(http.StatusInternalServerError)
			log.Printf("ERROR in handler %v: %v" , fname, err)
		}

		if res == nil {
			res = genericR{
			}
		}

		encoder := json.NewEncoder(w)
		if jerr := encoder.Encode(res); jerr != nil {
			log.Printf("ERROR in wrapper for %v handler, failed to encode result %v", fname, jerr)
		}
	}
}

// Can return empty WID
func getWID(session *melody.Session) (string, error) {
	if iwid, ok := session.Get(RpcKeyWID); ok && iwid != nil {
		if wid, ok := iwid.(string); ok {
			return wid, nil
		} else {
			return "", errors.New("invalid (non-string) wid stored on session")
		}
	}
	return "", nil
}

// Returns error if WID is empty
func getValidWID(session *melody.Session) (wid string, err error) {
	iwid, ok := session.Get(RpcKeyWID)

	if !ok {
		err = errors.New("no wallet id set in session")
		return
	}

	if iwid == nil {
		err = errors.New("nil wallet id")
		return
	}

	wid, ok = iwid.(string)
	if !ok {
		err = errors.New("invalid (non-string) wid stored on session")
		return
	}

	if len(wid) == 0 {
		err = errors.New("empty wallet id")
		return
	}

	return
}