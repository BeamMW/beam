package main

import (
	"fmt"
	"github.com/dustin/go-humanize"
	"log"
	"time"
)

func collectActivityLog () string {
	status := collectStatus(true)

	var svcEpoints int64
	var svcClients int64

	for _, svc := range status.WalletServices {
		svcEpoints += svc.EndpointsCnt
		svcClients += svc.ClientsCnt
	}

	return fmt.Sprintf("WalletServices: \n\tMax: %v\n\tAlive: %v\n\tDrops: %v\n\tEpoints: %v\n\tClients: %v\nMonitors:\n\tShould run: %v\n\tMax: %v\n\tAlive: %v\n\tDrops: %v",
						status.MaxWalletServices, status.AliveWalletServices,
						humanize.Comma(status.Counters.WSDrops),
						humanize.Comma(svcEpoints),
						humanize.Comma(svcClients),
						config.ShouldLaunchBBSMonitor(),
						status.MaxBbsServices, status.AliveBbsServices,
						humanize.Comma(status.Counters.BbsDrops),
	                  )
}

func startActivityLog () {
	var printActivity = func () {
		log.Printf("[====  Activity report  ====]\n%s\n[===========================]\n", collectActivityLog())
	}
	printActivity()
	ticker := time.NewTicker(config.ActivityLogInterval)
	go func() {
		for {
			<- ticker.C
			printActivity()
		}
	}()
}
