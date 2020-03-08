package main

import (
	"flag"
	"github.com/olahol/melody"
	"log"
	"net/http"
)

func main () {
	flag.Parse()
	args := flag.Args()

	if len(args) > 1 {
		log.Fatal("too many arguments")
	}

	if len(args) == 1 {
		if args[0] == "vapid-keys" {
			printNewVAPIDKeys()
			return
		} else {
			log.Fatalf("unknown command: %v", args[0])
		}
	}


	//
	// Command line is OK
	//
	log.Println("starting wallet service balancer")
	m := melody.New()

	if err := loadConfig(m); err != nil {
		log.Fatal(err)
	}

	if err := monitorInitialize(m); err != nil {
		log.Fatal(err)
	}

	//
	// HTTP API
	//

	// Now just hello
	http.HandleFunc("/", helloRequest)

	// Get endpoint
	http.HandleFunc("/login", wrapHandler(loginRequest))

	// Inform that wallet is still alive and endpoint should be kept
	http.HandleFunc("/alive", wrapHandler(aliveRequest))

	// Inform that wallet web client is leaving
	http.HandleFunc("/logout", wrapHandler(logoutRequest))

	// Get general server statistics
	http.HandleFunc("/status", wrapHandler(statusRequest))

	//
	// JsonRPCv2.0 over WebSockets
	//
	var wsGenericError = "websocket processing error, %v"
	http.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request){
		if err := m.HandleRequest(w, r); err != nil {
			log.Printf(wsGenericError, err)
		}
	})

	m.HandleMessage(func(session *melody.Session, msg []byte) {
		resp := jsonRpcProcess(session, msg)
		if err := session.Write(resp); err != nil {
			log.Printf(wsGenericError, err)
		}
	})

	m.HandleDisconnect(func(session *melody.Session) {
		if err := rpcDisconnect(session); err != nil {
			log.Printf(wsGenericError, err)
		}
	})

	m.HandlePong(func(session *melody.Session) {
		if err := rpcAlive(session); err != nil {
			log.Printf(wsGenericError, err)
		}
	})

	log.Println(config.ListenAddress, "Go!")
	if err := http.ListenAndServe(config.ListenAddress, nil); err != nil {
		log.Fatalf("Failed to start server %v", err)
	}
}
