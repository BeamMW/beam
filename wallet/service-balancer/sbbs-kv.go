package main

import (
	"encoding/json"
	"errors"
	badger "github.com/dgraph-io/badger/v2"
	"log"
	"time"
)

var (
	DB *badger.DB
)

const (
	ServerKey = "ServerKey"
	LerverKeyLen = len(ServerKey)
)

type Subscription struct {
	SbbsAddress          string `json:"SbbsAddress"`
	SbbsAddressPrivate   string `json:"SbbsAddressPrivate"`
	NotificationEndpoint string `json:"NotificationEndpoint"`
	P256dhKey            string `json:"P256dhKey"`
	AuthKey              string `json:"AuthKey"`
	ExpiresAt            int64  `json:"ExpiresAt"`
}

func sbbsDBInit() (err error) {
	if DB, err = badger.Open(badger.DefaultOptions(config.DatabasePath)); err != nil {
		return
	}

	err = DB.Update(func(tx *badger.Txn) error {
		item, err := tx.Get([]byte(ServerKey))

		if err == badger.ErrKeyNotFound {
			err = tx.Set([]byte(ServerKey), []byte(config.VAPIDPublic))
			return err
		}

		if err != nil {
			return err
		}

		err = item.Value(func(val []byte) error {
			if string(val) != config.VAPIDPublic {
				return errors.New("bad vapid server key stored in database")
			}
			return nil
		})

		return nil
	})

	if err != nil {
		return err
	}

	go func() {
		ticker := time.NewTicker(5 * time.Minute)
		defer ticker.Stop()
		for range ticker.C {
		again:
			err := DB.RunValueLogGC(0.7)
			if err == nil {
				goto again
			}
		}
	} ()

	return nil
}

func makeKey(addr, epoint string) []byte {
	return []byte(addr + "-" + epoint)
}

func makeValue(params* SubParams) ([]byte, error) {
	sub := Subscription{
		SbbsAddress:          params.SbbsAddress,
		SbbsAddressPrivate:   params.SbbsAddressPrivate,
		NotificationEndpoint: params.NotificationEndpoint,
		P256dhKey:            params.P256dhKey,
		AuthKey:              params.AuthKey,
		ExpiresAt:            params.ExpiresAt,
	}
	return json.Marshal(&sub)
}

func storeSub(params *SubParams) error {
	value, err := makeValue(params)
	if err != nil {
		return err
	}

	return DB.Update(func(tx *badger.Txn) error {
		key     := makeKey(params.SbbsAddress, params.NotificationEndpoint)
		expires := time.Until(time.Unix(params.ExpiresAt, 0))
		entry   := badger.NewEntry(key, value).WithTTL(expires)
		return tx.SetEntry(entry)
	})
}

func removeSub(params *UnsubParams) error {
	return DB.Update(func(tx *badger.Txn) error {
		key := makeKey(params.SbbsAddress, params.NotificationEndpoint)
		item, err := tx.Get(key)

		if err != nil {
			return err
		}

		return item.Value(func(val []byte) error {
			var sub Subscription
			if err := json.Unmarshal(val, &sub); err != nil {
				return err
			}

			if sub.SbbsAddressPrivate != params.SbbsAddressPrivate {
				return errors.New("bad sbbs private key, unauthorized request rejeted")
			}

			if err = tx.Delete(key); err != nil {
				return err
			}

			if config.Debug {
				log.Printf("subscription %v:%v removed", params.SbbsAddress, params.NotificationEndpoint)
			}

			return nil
		})
	})
}

func forAllSubs(handler func(entry *Subscription)) error {
	return DB.View(func(tx *badger.Txn) error {
		opts := badger.DefaultIteratorOptions
		iter := tx.NewIterator(opts)
		defer iter.Close()

		for iter.Rewind(); iter.Valid(); iter.Next() {
			item := iter.Item()

			if len(item.Key()) == LerverKeyLen {
				// dirty hack to skip sys data
				continue
			}

			err := item.Value(func(val []byte) error {
				var sub Subscription
				if err := json.Unmarshal(val, &sub); err != nil {
					return err
				}
				handler(&sub)
				return nil
			})

			if err != nil {
				return err
			}
		}

		return nil
	})
}

func forEachSub(sbssAddr string, handler func(entry *Subscription)) error {
	return DB.View(func(tx *badger.Txn) error {
		iter := tx.NewIterator(badger.DefaultIteratorOptions)
		defer iter.Close()

		prefix := []byte(sbssAddr)
		for iter.Seek(prefix); iter.ValidForPrefix(prefix); iter.Next() {
			item := iter.Item()

			err := item.Value(func(val []byte) error {
				var sub Subscription
				if err := json.Unmarshal(val, &sub); err != nil {
					return err
				}
				handler(&sub)
				return nil
			})

			if err != nil {
				return err
			}
		}

		return nil
	})
}
