package main

import (
	"fmt"
	"github.com/SherClockHolmes/webpush-go"
	"github.com/ethereum/go-ethereum/crypto/secp256k1"
	"log"
	"math/big"
)

//
// Do not use crypto/elliptic since it represents a short-form Weierstrass curve with a=-3
// secp256k1 does not meet that requirement
//

func printNewVAPIDKeys() {
	private, public, err := webpush.GenerateVAPIDKeys()
	if err != nil {
		log.Fatalf("Error in VAPID keys generation, %v", err)
	}

	log.Printf("New VAPID keys")
	log.Printf("\tPrivate Base64-encoded: %v", private)
	log.Printf("\tPublic Base64-encoded: %v", public)
}

func checkBbsKeys(bbsAddr string, private string) bool {
	biPriv, ok := big.NewInt(0).SetString(private, 16)
	if !ok {
		return false
	}

	curve := secp256k1.S256()
	derivedPub, _ := curve.ScalarBaseMult(biPriv.Bytes())

	derivedPubStr := fmt.Sprintf("%x", derivedPub)
	sliceStart := len(bbsAddr)-len(derivedPubStr)
	if sliceStart < 0 {
		return false
	}
	bbsPub := bbsAddr[sliceStart:]
	return bbsPub == derivedPubStr
}
