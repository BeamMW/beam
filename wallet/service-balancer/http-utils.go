package main 

import (
	"net/http"
)

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
