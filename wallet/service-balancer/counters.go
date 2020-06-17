package main

import (
	"sync/atomic"
	"time"
)

//
// Here we just gather some stats
//
type Counters struct{
	WUpgrade        int64
	WUpgrade30      int64
	WReject         int64
	WReject30       int64
	WConnect	    int64
	WConnect30      int64
	Login           int64
	Login30         int64
	WDisconnect     int64
	WDisconnect30   int64
	Logout          int64
	Logout30        int64
	Subscribe       int64
	Subscribe30     int64
	Unsubscribe     int64
	Unsubscribe30   int64
	WBadMethod      int64
	WBadMethod30    int64
	WError          int64
	WError30        int64
	BbsMessage      int64
	BbsMessage30    int64
	BbsBadMethod    int64
	BbsBadMethod30  int64
	BbsError        int64
	BbsError30      int64
	BbsDrops        int64
	BbsDrops30      int64
	WSDrops         int64
	WSDrops30       int64
	EPoitDrops      int64
	EPoitDrops30    int64
	EPClientDrops   int64
	EPClientDrops30 int64
}

func (c* Counters) CopyTo(to *Counters) {
	to.Login 		   =  atomic.LoadInt64(&c.Login)
	to.Login30		   =  atomic.LoadInt64(&c.Login30)
	to.Logout 		   =  atomic.LoadInt64(&c.Logout)
	to.Logout30   	   =  atomic.LoadInt64(&c.Logout30)
	to.Subscribe   	   =  atomic.LoadInt64(&c.Subscribe)
	to.Subscribe30 	   =  atomic.LoadInt64(&c.Subscribe30)
	to.Unsubscribe     =  atomic.LoadInt64(&c.Unsubscribe)
	to.Unsubscribe30   =  atomic.LoadInt64(&c.Unsubscribe30)
	to.WBadMethod      =  atomic.LoadInt64(&c.WBadMethod)
	to.WBadMethod30    =  atomic.LoadInt64(&c.WBadMethod30)
	to.WUpgrade        =  atomic.LoadInt64(&c.WUpgrade)
	to.WUpgrade30      =  atomic.LoadInt64(&c.WUpgrade30)
	to.WReject         =  atomic.LoadInt64(&c.WReject)
	to.WReject30       =  atomic.LoadInt64(&c.WReject30)
	to.WConnect        =  atomic.LoadInt64(&c.WConnect)
	to.WConnect30      =  atomic.LoadInt64(&c.WConnect30)
	to.WDisconnect     =  atomic.LoadInt64(&c.WDisconnect)
	to.WDisconnect30   =  atomic.LoadInt64(&c.WDisconnect30)
	to.WError          =  atomic.LoadInt64(&c.WError)
	to.WError30        =  atomic.LoadInt64(&c.WError30)
	to.BbsDrops        =  atomic.LoadInt64(&c.BbsDrops)
	to.BbsDrops30      =  atomic.LoadInt64(&c.BbsDrops30)
	to.WSDrops         =  atomic.LoadInt64(&c.WSDrops)
	to.WSDrops30       =  atomic.LoadInt64(&c.WSDrops30)
	to.EPoitDrops      =  atomic.LoadInt64(&c.EPoitDrops)
	to.EPoitDrops30    =  atomic.LoadInt64(&c.EPoitDrops30)
	to.EPClientDrops   =  atomic.LoadInt64(&c.EPClientDrops)
	to.EPClientDrops30 =  atomic.LoadInt64(&c.EPClientDrops30)
	to.BbsMessage 	   =  atomic.LoadInt64(&c.BbsMessage)
	to.BbsMessage30    =  atomic.LoadInt64(&c.BbsMessage30)
	to.BbsBadMethod    =  atomic.LoadInt64(&c.BbsBadMethod)
	to.BbsBadMethod30  =  atomic.LoadInt64(&c.BbsBadMethod30)
	to.BbsError        =  atomic.LoadInt64(&c.BbsError)
	to.BbsError30      =  atomic.LoadInt64(&c.BbsError30)
}

func (c *Counters) CountLogin() {
	atomic.AddInt64(&c.Login, 1)
	atomic.AddInt64(&c.Login30, 1)
}

func (c *Counters) CountLogout() {
	atomic.AddInt64(&c.Logout, 1)
	atomic.AddInt64(&c.Logout30, 1)
}

func (c *Counters) CountSubscribe() {
	atomic.AddInt64(&c.Subscribe, 1)
	atomic.AddInt64(&c.Subscribe30, 1)
}

func (c *Counters) CountUnsubscribe() {
	atomic.AddInt64(&c.Unsubscribe, 1)
	atomic.AddInt64(&c.Unsubscribe30, 1)
}

func (c *Counters) CountWBadMethod() {
	atomic.AddInt64(&c.WBadMethod, 1)
	atomic.AddInt64(&c.WBadMethod30, 1)
}

func (c *Counters) CountWUpgrade() {
	atomic.AddInt64(&c.WUpgrade, 1)
	atomic.AddInt64(&c.WUpgrade30, 1)
}

func (c *Counters) CountWReject() {
	atomic.AddInt64(&c.WReject, 1)
	atomic.AddInt64(&c.WReject30, 1)
}

func (c *Counters) CountWConnect() {
	atomic.AddInt64(&c.WConnect, 1)
	atomic.AddInt64(&c.WConnect30, 1)
}

func (c *Counters) CountWDisconnect() {
	atomic.AddInt64(&c.WDisconnect, 1)
	atomic.AddInt64(&c.WDisconnect30, 1)
}

func (c *Counters) CountWError() {
	atomic.AddInt64(&c.WError, 1)
	atomic.AddInt64(&c.WError30, 1)
}

func (c *Counters) CountBBSDrop() {
	atomic.AddInt64(&c.BbsDrops, 1)
	atomic.AddInt64(&c.BbsDrops30, 1)
}

func (c *Counters) CountWSDrop(epoints int64, clients int64) {
	atomic.AddInt64(&c.WSDrops, 1)
	atomic.AddInt64(&c.WSDrops30, 1)
	atomic.AddInt64(&c.EPoitDrops, epoints)
	atomic.AddInt64(&c.EPoitDrops30, epoints)
	atomic.AddInt64(&c.EPClientDrops, clients)
	atomic.AddInt64(&c.EPClientDrops30, clients)
}

func (c *Counters) CountBbsMessage() {
	atomic.AddInt64(&c.BbsMessage, 1)
	atomic.AddInt64(&c.BbsMessage30, 1)
}

func (c* Counters) CountBbsBadMethod() {
	atomic.AddInt64(&c.BbsBadMethod, 1)
	atomic.AddInt64(&c.BbsBadMethod30, 1)
}

func (c* Counters) CountBbsError() {
	atomic.AddInt64(&c.BbsError, 1)
	atomic.AddInt64(&c.BbsError30, 1)
}

func (c* Counters) Reset30() {
	atomic.StoreInt64(&c.Login30, 0)
	atomic.StoreInt64(&c.Logout30, 0)
	atomic.StoreInt64(&c.Subscribe30, 0)
	atomic.StoreInt64(&c.Unsubscribe30, 0)
	atomic.StoreInt64(&c.WBadMethod30, 0)
	atomic.StoreInt64(&c.WUpgrade30, 0)
	atomic.StoreInt64(&c.WReject30, 0)
	atomic.StoreInt64(&c.WConnect30, 0)
	atomic.StoreInt64(&c.WDisconnect30, 0)
	atomic.StoreInt64(&c.WError30, 0)
	atomic.StoreInt64(&c.BbsDrops30, 0)
	atomic.StoreInt64(&c.WSDrops30, 0)
	atomic.StoreInt64(&c.EPoitDrops30, 0)
	atomic.StoreInt64(&c.EPClientDrops30, 0)
	atomic.StoreInt64(&c.BbsMessage30, 0)
	atomic.StoreInt64(&c.BbsBadMethod30, 0)
	atomic.StoreInt64(&c.BbsError30, 0)
}

var (
	counters *Counters
)

func startCounters () {
	counters = &Counters{}
	go func() {
		ticker := time.NewTicker(30 * time.Minute)
		for {
			<- ticker.C
			counters.Reset30()
		}
	}()
}
