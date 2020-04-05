package wsclient

import (
	"context"
	"errors"
	"github.com/gorilla/websocket"
	"net/url"
	"sync"
	"time"
)

type fnError   func(*WSClient, error)
type fnMessage func(*WSClient, []byte)

type WSClient struct {
	conn       *websocket.Conn
	url        *url.URL
	mutex      sync.Mutex
	context    context.Context
	cancel     context.CancelFunc
	send       chan interface{}
	pingPeriod time.Duration
	pongWait   time.Duration
	onError    fnError
	onMessage  fnMessage
}

func NewWSClient(host string, path string) (*WSClient, error) {
	var pongWait = 30 * time.Second
	var pingPeriod = pongWait * 9 / 10

	client := WSClient{
		url:        &url.URL{Scheme: "ws", Host: host, Path: path},
		send:       make(chan interface{}),
		pingPeriod: pingPeriod,
		pongWait:   pongWait,
		onError:    func(*WSClient, error) {},
		onMessage:  func(*WSClient, []byte) {},
	}

	client.context, client.cancel = context.WithCancel(context.Background())

	go client.readLoop()
	go client.writeLoop()

	return &client, nil
}

func (client *WSClient) HandleMessage(handler fnMessage) {
	client.onMessage = handler
}

func (client *WSClient)  HandleError(handler fnError){
	client.onError = handler
}

func (client *WSClient) Connect() *websocket.Conn {
	client.mutex.Lock()
	defer client.mutex.Unlock()

	if client.conn != nil {
		return client.conn
	}

	retry := time.NewTicker(time.Second)
	defer retry.Stop()

	for ;; <-retry.C {
		select {
		case <- client.context.Done():
			return nil
		default:
			conn, _, err := websocket.DefaultDialer.Dial(client.url.String(), nil)
			if err != nil {
				client.onError(client, err)
				continue
			}

			_ = conn.SetReadDeadline(time.Now().Add(client.pongWait))
			conn.SetPongHandler(func(string) error {
				_ = conn.SetReadDeadline(time.Now().Add(client.pongWait))
				return nil
			})

			client.conn = conn
			return conn
		}
	}
}

func (client *WSClient) Write(what interface{}) {
	client.send <- what
}

func (client *WSClient) Stop() {
	client.cancel()
	client.closeSocket()
}

func (client *WSClient) closeSocket() {
	client.mutex.Lock()
	defer client.mutex.Unlock()

	if conn := client.conn; conn != nil {
		err := conn.WriteMessage(websocket.CloseMessage, websocket.FormatCloseMessage(websocket.CloseNormalClosure, ""))
		if err != nil {
			client.onError(client, err)
		}

		err = conn.Close()
		if err != nil {
			client.onError(client, err)
		}

		client.conn = nil
	}
}

func (client *WSClient) readLoop () {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	for {
		select {
		case <- client.context.Done():
			return
		case <-ticker.C:
			for {
				conn := client.Connect()
				if conn == nil {
					continue
				}

				t, message, err := conn.ReadMessage()
				if err != nil {
					client.onError(client, err)
					client.closeSocket()
					continue
				}

				if t == websocket.TextMessage {
					client.onMessage(client, message)
				}

				if t == websocket.BinaryMessage {
					client.onError(client, errors.New("unexpected binary message"))
				}
			}
		}
	}
}

func (client *WSClient) writeLoop() {
	ping := time.NewTicker(client.pingPeriod)
	defer ping.Stop()

	for {
		select {
		case <- ping.C:
			conn := client.Connect()
			if conn == nil {
				client.onError(client, errors.New("wsclient failed to connecti in write loop, ping skipped"))
				continue
			}
			if err := conn.WriteControl(websocket.PingMessage, []byte{}, time.Now().Add(client.pingPeriod/2)); err != nil {
				client.onError(client, err)
				client.closeSocket()
				continue
			}
		case data := <- client.send:
			conn := client.Connect()
			if conn == nil {
				client.onError(client, errors.New("wsclient failed to connect in write loop, message skipped"))
				continue
			}

			var err error
			if bytes, ok := data.([]byte); ok {
				err = conn.WriteMessage(websocket.TextMessage, bytes)
			} else {
				err = conn.WriteJSON(data)
			}
			if err != nil {
				client.onError(client, err)
				client.closeSocket()
				continue
			}

		case <- client.context.Done():
			return
		}
	}
}
