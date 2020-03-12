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
	startCounters()
	m := melody.New()

	if err := loadConfig(m); err != nil {
		log.Fatal(err)
	}

	if err := sbbsMonitorInitialize(); err != nil {
		log.Fatal(err)
	}

	if err := walletServicesInitialize(m); err != nil {
		log.Fatal(err)
	}

	//
	// HTTP API
	//

	// Now just hello
	http.HandleFunc("/", helloRequest)

	// Get general server statistics
	http.HandleFunc("/status", wrapHandler(statusRequest))

	//
	// JsonRPCv2.0 over WebSockets
	//
	var wsGenericError = "websocket server processing error, %v"
	http.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request){
		counters.CountWUpgrade()
		if err := m.HandleRequest(w, r); err != nil {
			log.Printf(wsGenericError, err)
		}
	})

	m.HandleMessage(func(session *melody.Session, msg []byte) {
		go func() {
			if resp := jsonRpcProcessWallet(session, msg); resp != nil {
				if err := session.Write(resp); err != nil {
					log.Printf(wsGenericError, err)
				}
			}
		} ()
	})

	m.HandleConnect(func(session *melody.Session) {
		counters.CountWConnect()
		log.Printf("websocket server new session")
	})

	m.HandleDisconnect(func(session *melody.Session) {
		counters.CountWDisconnect()
		if err := onWalletDisconnect(session); err != nil {
			log.Printf(wsGenericError, err)
		}
	})

	m.HandlePong(func(session *melody.Session) {
		if err := onWalletPong(session); err != nil {
			log.Printf(wsGenericError, err)
		}
	})

	m.HandleError(func(session *melody.Session, err error) {
		counters.CountWError()
		log.Printf(wsGenericError, err)
	})

	log.Println(config.ListenAddress, "Go!")
	if err := http.ListenAndServe(config.ListenAddress, nil); err != nil {
		log.Fatalf("Failed to start server %v", err)
	}
}
