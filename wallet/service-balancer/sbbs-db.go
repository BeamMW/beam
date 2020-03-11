package main

import (
	"encoding/json"
	"errors"
	"fmt"
	tiedot "github.com/HouzuoGuo/tiedot/db"
	"log"
)

const (
	DBPath     = "./sub-data"
	SubsColl   = "Subscriptions"
	ConfigColl = "Config"
	ServerKey  = "ServerKey"
)

var (
	DB *tiedot.DB
	Subs *tiedot.Col
)

func sbbsDBInit() (err error) {
	// TODO: explore options
	if DB, err = tiedot.OpenDB(DBPath); err != nil {
		return
	}

	if !DB.ColExists(SubsColl) {
		if err = DB.Create(SubsColl); err != nil {
			return
		}

		Subs = DB.Use(SubsColl)
		if Subs == nil {
			err = errors.New("failed to get Subscriptions collection")
			return
		}

		if err = Subs.Index([]string{"SbbsAddress"}); err != nil {
			return
		}

		if err = Subs.Index([]string{"NotificationEndpoint"}); err != nil {
			return
		}

		if err = DB.Create(ConfigColl); err != nil {
			return
		}

		Config := DB.Use(ConfigColl)
		if Config == nil {
			err = errors.New("failed to get Config collection")
			return
		}

		if _, err = Config.Insert(map[string]interface{}{
			ServerKey: config.VAPIDPublic,
		}); err != nil {
			return
		}
	} else {
		Subs = DB.Use(SubsColl)
		if Subs == nil {
			err = errors.New("failed to get Subscriptions collection")
			return
		}

		Config := DB.Use(ConfigColl)
		if Config == nil {
			err = errors.New("failed to get Config collection")
			return
		}

		var wrongKey = true
		Config.ForEachDoc(func (id int, doc[] byte) bool {
			var entry map[string]interface{}
			if err := json.Unmarshal(doc, &entry); err != nil {
				log.Printf("config for each %v", err)
				return true
			}
			skey := entry[ServerKey]
			wrongKey = skey != config.VAPIDPublic
			return true
		})
		if wrongKey  {
			return errors.New("wrong VAPID key in database or no key stored")
		}
	}

	return nil
}

type subEntry struct {
	SbbsAddress          string `json:"SbbsAddress"`
	SbbsAddressPrivate   string `json:"SbbsAddressPrivate"`
	NotificationEndpoint string `json:"NotificationEndpoint"`
	P256dhKey            string `json:"P256dhKey"`
	AuthKey              string `json:"AuthKey"`
}

func hasSub(params *rpcSubscribeParams) (bool, error) {
	qfmt := `{"n": [{"eq": "%v", "in": ["SbbsAddress"]}, {"eq": "%v", "in": ["NotificationEndpoint"]}]}`
	qstr := fmt.Sprintf(qfmt, params.SbbsAddress, params.NotificationEndpoint)

	var query interface{}
	if err := json.Unmarshal([]byte(qstr), &query); err != nil {
		return false, err
	}

	var qres = make(map[int]struct{})
	if err := tiedot.EvalQuery(query, Subs, &qres); err != nil {
		return false, err
	}

	return len(qres) != 0, nil
}

func storeSub(params *rpcSubscribeParams) error {
	have, err := hasSub(params)
	if err != nil {
		return err
	}

	if have {
		// already stored
		// TODO: update TTL
		return nil
	}

	if _, err = Subs.Insert(map[string]interface{}{
		"SbbsAddress": params.SbbsAddress,
		"SbbsAddressPrivate": params.SbbsAddressPrivate,
		"NotificationEndpoint": params.NotificationEndpoint,
		"P256dhKey": params.P256dhKey,
		"AuthKey": params.AuthKey,
	}); err != nil {
		return err
	}

	return nil
}

func forEachSub(sbssAddr string, handler func(entry *subEntry)) error {
	qfmt := `{"eq": "%v", "in": ["SbbsAddress"]}`
	qstr := fmt.Sprintf(qfmt, sbssAddr)

	var query interface{}
	if err := json.Unmarshal([]byte(qstr), &query); err != nil {
		return err
	}

	var qres = make(map[int]struct{})
	if err := tiedot.EvalQuery(query, Subs, &qres); err != nil {
		return err
	}

	for docid := range qres {
		vals, err := Subs.Read(docid)
		if err != nil {
			return err
		}
		handler(&subEntry{
			SbbsAddress: vals["SbbsAddress"].(string),
			SbbsAddressPrivate: vals["SbbsAddressPrivate"].(string),
			NotificationEndpoint: vals["NotificationEndpoint"].(string),
			P256dhKey: vals["P256dhKey"].(string),
			AuthKey: vals["AuthKey"].(string),
		})
	}

	return nil
}

func forAllSubs(handler func(entry *subEntry)) {
	Subs.ForEachDoc(func (id int, doc[] byte) bool {
		var entry subEntry
		if err := json.Unmarshal(doc, &entry); err != nil {
			log.Printf("forAllSubs error %v", err)
			return true
		}
		handler(&entry)
		return true
	})
}
