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

// TODO: allow CORs only for hosts from config
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

func setJsonHeaders(w http.ResponseWriter, r *http.Request) {
	w.Header().Add("Content-Type", "application/json; charset=utf-8")
}

type genericR struct {
	Error string  `json:"error,omitempty"`
}

func wrapHandler(handler func(r *http.Request)(interface{}, error)) http.HandlerFunc{
	var handlerName = runtime.FuncForPC(reflect.ValueOf(handler).Pointer()).Name()
	return func (w http.ResponseWriter, r *http.Request) {
		allowCORS(w, r)
		setJsonHeaders(w, r)

		if r.Method == "OPTIONS" {
			return
		}

		var res interface{} = nil
		res, err := handler(r)

		if err != nil {
			res = genericR{
				Error: err.Error(),
			}
			w.WriteHeader(http.StatusInternalServerError)
			log.Printf("ERROR in handler %v: %v" , handlerName, err)
		}

		if res == nil {
			res = genericR{
			}
		}

		encoder := json.NewEncoder(w)
		if jerr := encoder.Encode(res); jerr != nil {
			log.Printf("ERROR in wrapper for %v handler, failed to encode result %v", handlerName, jerr)
		}
	}
}

func getValidWID(session *melody.Session) (wid string, err error) {
	stored, ok := session.Get(RpcKeyWID)

	if !ok {
		err = errors.New("no wallet id set")
		return
	}

	wid, ok = stored.(string)
	if !ok {
		err = errors.New("non-string or nil wid")
		return
	}

	if len(wid) == 0 {
		err = errors.New("empty wallet id")
		return
	}

	return
}
