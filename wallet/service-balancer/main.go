package main

import (
	"log"
	"net/http"
)

func main () {
	log.Println("Starting wallet service balancer")

	if err := loadConfig(); err != nil {
		log.Fatal(err)
	}

	if err := monitor2Initialize(); err != nil {
		log.Fatal(err)
	}

	// Now just hello
	http.HandleFunc("/", helloRequest)

	// Get endpoint
	http.HandleFunc("/login", loginRequest2)

	// Inform that wallet is still alive and endpoint should be kept
	// TODO: consider websockets and automatically detect disconnect
	http.HandleFunc("/alive", aliveRequest2)

	// Forcibly close endpoint and stop service
	// Should normally not be used
	http.HandleFunc("/logout", logoutRequest2)
	
	log.Println(config.ListenAddress, "Go!")
	if err := http.ListenAndServe(config.ListenAddress, nil); err != nil {
		log.Fatal(err)
	}

	// TODO: implement graceful shutdown
	// TODO: do not exit main() until all monitors are done
	// TODO: consider implementing logs separation, DEBUG/INFO/WARNING
	// TODO: consider refactoring handlers, get rid of copy-paste request parsing & error handling code
}
