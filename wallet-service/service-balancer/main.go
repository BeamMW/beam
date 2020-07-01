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
	log.Printf("cwd is %s", getCWD())

	startCounters()
	m := melody.New()

	if err := loadConfig(m); err != nil {
		log.Fatal(err)
	}

	if config.ShouldLaunchBBSMonitor() {
		if err := sbbsMonitorInitialize(); err != nil {
			log.Fatal(err)
		}
	} else {
		log.Println("WARNING: BBS Monitor is not launched. VAPIDPublic or VAPIDPrivate or PushContactMail is not provided")
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
	http.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request){
		if err := checkOrigin(r); err != nil {
			counters.CountWReject()
			w.WriteHeader(http.StatusForbidden)
			_,_ = w.Write([]byte(err.Error()))
			return
		}
		counters.CountWUpgrade()
		if err := m.HandleRequest(w, r); err != nil {
			log.Printf("websocket handle request error, %v", err)
		}
	})

	m.HandleMessage(func(session *melody.Session, msg []byte) {
		go func() {
			if resp := jsonRpcProcessWallet(session, msg); resp != nil {
				if err := session.Write(resp); err != nil {
					log.Printf("websocket jsonRpcProcessWallet error, %v", err)
				}
			}
		} ()
	})

	m.HandleConnect(func(session *melody.Session) {
		counters.CountWConnect()
		if config.Debug {
			log.Printf("websocket server new session")
		}
	})

	m.HandleDisconnect(func(session *melody.Session) {
		counters.CountWDisconnect()
		if err := onWalletDisconnect(session); err != nil {
			log.Printf("websocket onWalletDisconnect error, %v", err)
		}
	})

	m.HandlePong(func(session *melody.Session) {
		if err := onWalletPong(session); err != nil {
			log.Printf("websocket onWalletPong error, %v", err)
		}
	})

	m.HandleError(func(session *melody.Session, err error) {
		counters.CountWError()
		if config.Debug {
			log.Printf("websocket error, %v", err)
		}
	})

	startActivityLog()

	log.Println(config.ListenAddress, "Go!")
	if err := http.ListenAndServe(config.ListenAddress, nil); err != nil {
		log.Fatalf("Failed to start server %v", err)
	}
}
