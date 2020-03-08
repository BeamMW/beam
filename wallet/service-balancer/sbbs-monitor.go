package main

import (
	"github.com/gorilla/websocket"
	"log"
	"net/url"
)

func sbbsListen() {
	var sbbsIdx = 0
	sbbs, err := sbbsServices.GetAt(sbbsIdx)
	if err != nil {
		log.Fatalf("Failed to get bbs 0")
	}

	u := url.URL{Scheme: "ws", Host: sbbs.Address, Path: "/"}
	log.Printf("bbs %v, connecting to %s", sbbsIdx, u.String())

	conn, _, err := websocket.DefaultDialer.Dial(u.String(), nil)
	if err != nil {
		log.Fatal("bbs %v, dial error %v", sbbsIdx, err)
	}
	//defer conn.Close()

	done := make(chan struct{})
	go func() {
		defer close(done)
		for {
			_, message, err := conn.ReadMessage()
			if err != nil {
				log.Println("read:", err)
				return
			}
			log.Printf("recv: %s", message)
		}
	}()
}
