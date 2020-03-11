package main

import (
	"github.com/SherClockHolmes/webpush-go"
	"log"
)

func printNewVAPIDKeys() {
	private, public, err := webpush.GenerateVAPIDKeys()
	if err != nil {
		log.Fatalf("Error in VAPID keys generation, %v", err)
	}

	log.Printf("New VAPID keys")
	log.Printf("\tPrivate Base64-encoded: %v", private)
	log.Printf("\tPublic Base64-encoded: %v", public)
}
