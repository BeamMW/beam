package main

import (
	"net/http"
	"fmt"
)

func helloRequest(w http.ResponseWriter, r *http.Request) {
	_,_ = fmt.Fprintf(w, "Hello! This is the wallet service balancer")
}
