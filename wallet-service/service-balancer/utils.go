package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"github.com/olahol/melody"
	"log"
	"net/http"
	"os"
	"reflect"
	"runtime"
)

func setJsonHeaders(w http.ResponseWriter, r *http.Request) {
	w.Header().Add("Content-Type", "application/json; charset=utf-8")
}

type genericR struct {
	Error string  `json:"error,omitempty"`
}

func wrapHandler(handler func(r *http.Request)(interface{}, error)) http.HandlerFunc{
	var handlerName = runtime.FuncForPC(reflect.ValueOf(handler).Pointer()).Name()
	return func (w http.ResponseWriter, r *http.Request) {
		setJsonHeaders(w, r)

		if r.Method == "OPTIONS" {
			return
		}

		res, err := handler(r)

		if err != nil {
			res = genericR{
				Error: err.Error(),
			}
			w.WriteHeader(http.StatusInternalServerError)
			log.Printf("ERROR in handler %v: %v" , handlerName, err)
		}

		if res == nil {
			res = genericR {
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

func checkOrigin(r *http.Request) error {
	if len(config.AllowedOrigin) == 0 {
		return nil
	}

	origin := r.Header.Get("Origin")
	if len(origin) == 0 {
		return errors.New("origin is not set")
	}

	if origin != config.AllowedOrigin {
		return fmt.Errorf("origin is not allowed: %v", origin)
	}

	return nil
}

func getCWD() string {
	res, err := os.Getwd()
	if err != nil {
		log.Printf("Error while getting working directory, %v", err)
		return ""
	}
	return res
}
