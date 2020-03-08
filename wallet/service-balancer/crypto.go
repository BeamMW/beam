package main

import (
	"crypto/elliptic"
	"crypto/rand"
	"encoding/base64"
	"errors"
	"log"
	"math/big"
)

type PrivateKey struct {
	Raw    []byte
	Base64 string
}

type PublicKey struct {
	Uncompressed []byte // Uncompressed public key as per ANS X9.62-2005 section 3
	Base64 string
	RawX *big.Int
	RawY *big.Int
}

func getCurve() elliptic.Curve {
	return elliptic.P256()
}

func GenerateVAPIDKeys() (priv *PrivateKey, pub *PublicKey, err error) {
	curve := getCurve()
	private, pubX, pubY, err := elliptic.GenerateKey(curve, rand.Reader)

	if err != nil {
		return
	}

	priv = &PrivateKey{
		Raw:    private,
		Base64: base64.RawURLEncoding.EncodeToString(private),
	}

	public := elliptic.Marshal(curve, pubX, pubY)
	pub = &PublicKey{
		Uncompressed: public,
		Base64:       base64.RawURLEncoding.EncodeToString(public),
		RawX:         pubX,
		RawY:         pubY,
	}

	return
}

func LoadVAPIDPrivateKey(keyBase64 string) (*PrivateKey, error) {
	raw, err := base64.RawURLEncoding.DecodeString(keyBase64)

	if err != nil {
		return nil, err
	}

	if len(raw) != 32 {
		return nil, errors.New("bad private key size")
	}

	key := PrivateKey{
		Base64: keyBase64,
		Raw: raw,
	}

	return &key, nil
}

func LoadVAPIDPublicKey(keyBase64 string) (*PublicKey, error) {
	raw, err := base64.RawURLEncoding.DecodeString(keyBase64)
	if err != nil {
		return nil, err
	}

	if len(raw) != 65 {
	    return nil, errors.New("bad public key size")
	}

	x, y := elliptic.Unmarshal(getCurve(), raw)
	if x == nil || y == nil {
		return nil, errors.New("failed to unpack bytes array")
	}

	key := PublicKey {
		Uncompressed: raw,
		Base64:       keyBase64,
		RawX:         x,
		RawY:         y,
	}

	return &key, nil
}

func printNewVAPIDKeys() {
	private, public, err := GenerateVAPIDKeys()
	if err != nil {
		log.Fatalf("Error in VAPID keys generation, %v", err)
	}

	log.Printf("New VAPID keys")
	log.Printf("\tPrivate Base64-encoded: %v", private.Base64)
	log.Printf("\tPublic Base64-encoded: %v", public.Base64)
}
