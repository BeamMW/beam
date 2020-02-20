package main

import (
	"log"
	"net/http"
)

func main () {
	log.Println("starting wallet service balancer")

	if err := loadConfig(); err != nil {
		log.Fatal(err)
	}

	if err := monitorInitialize(); err != nil {
		log.Fatal(err)
	}

	// Now just hello
	http.HandleFunc("/", helloRequest)

	// Get endpoint
	http.HandleFunc("/login", wrapHandler(loginRequest))

	// Inform that wallet is still alive and endpoint should be kept
	// TODO: consider websockets and automatically detect disconnect
	http.HandleFunc("/alive", wrapHandler(aliveRequest))

	// Inform that wallet web client is leaving
	http.HandleFunc("/logout", wrapHandler(logoutRequest))

	log.Println(config.ListenAddress, "Go!")
	if err := http.ListenAndServe(config.ListenAddress, nil); err != nil {
		log.Fatalf("Failed to start server %v", err)
	}
}
