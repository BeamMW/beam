package main

import (
	"beam.mw/service-balancer/services"
	"fmt"
	"github.com/olahol/melody"
	"log"
	"strconv"
)

var (
	walletServices *services.Services
)


func walletServicesInitialize(m *melody.Melody) (err error) {
	walletServices, err = NewWalletServices()
	if err != nil {
		return
	}

	//
	// This is a connection point between services and endpoints
	//
	go func () {
		for {
			select {
			case svcIdx := <- walletServices.Dropped:
				points, clients := epoints.DropServiceEndpoints(svcIdx)
				counters.CountWSDrop(points, clients)
				log.Printf("service %v, dropped. %v endpoint(s) with %v client(s)", svcIdx, points, clients)
			case svcIdx := <- walletServices.Restarted:
				log.Printf("service %v, restarted", svcIdx)
			}
		}
	} ()

	return
}

func wallletServicesGet(wid string) (string, error) {
	if epoint, ok := epoints.Get(wid); ok {
		svcIdx, svcAddr := epoint.Use()
		if config.Debug {
			log.Printf("wallet %v, existing endpoint is [%v:%v]", wid, svcIdx, svcAddr)
		}
		return svcAddr, nil
	}

	//
	// Since balancer is concurrent new endpoint might be added by another
	// thread between epoints.Get and epoints.Add. Add() handles this case
	// and returns existing endpoint if necessary. This situation should
	// be very rare though possible
	//
	svcIdx, service, err := walletServices.GetNext()
	if err != nil {
		return "", fmt.Errorf("wallet %v, %v", wid, err)
	}

	var address string
	if config.ReturnRawSvcPort {
		address = config.ServicePublicAddress + ":" + strconv.Itoa(service.Port)
	} else {
		address = config.ServicePublicAddress + "?service=" + strconv.Itoa(service.Port)
	}

	epoints.Add(wid, svcIdx, address)

	if config.Debug {
		log.Printf("wallet %v, new endpoint is [%v:%v]", wid, svcIdx, address)
	}

	return address, nil
}

func walletServicesAlive (wid string) error {
	if epoint, ok := epoints.Get(wid); ok {
		epoint.WalletAlive <- true
		return nil
	}

	// This usually means issue in the web wallet code
	return fmt.Errorf("wallet %v, alive request on missing endpoint", wid)
}

func walletServicesLogout(wid string) error {
	if epoint, ok := epoints.Get(wid); ok {
		epoint.WalletLogout <- true
		return nil
	}

	// This usually means issue in the web wallet code
	return fmt.Errorf("wallet %v, logout request on missing endpoint", wid)
}
