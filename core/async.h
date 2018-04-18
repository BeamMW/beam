#pragma once

class Timer
{
	// TODO
public:
	virtual void OnTimer() = 0;

	void SetTimer(uint32_t timeout_ms); // 1-shot timer, not auto-recurrent
	void KillTimer();
};

class Connection
{
	// encapsulates socket
	// has rcv buffer (for partially-received messages)
	// has snd buffer (in case undelying socket isn't able to send the data right now)
public:

	struct Addr {
		// IPv.4
		uint32_t m_Addr;
		uint16_t m_Port;
	};

	class Interface
	{
		// encapsulates 'listening' socket
	public:
		void Listen(const Addr&);
		void StopListening();

		virtual void OnAccept() {}
	};

	// all the methods are non-blocking!
	// In case of error exception should be thrown
	void Connect(const Addr&);
	void Accept(Interface&);

	Addr get_LocalAddr();
	Addr get_PeerAddr();

	void Disconnect();

	void Send(const void* p, uint32_t n); // can send *before* the connection is actually established
	// If sending isn't possible right now (not connected, or chocking) - the data is copied to the send buffer

	// callbacks
	virtual void OnConnected() {}

	virtual void OnSnd() {
		// some data (but not necessarily all) was consumed from the send buffer.
	}

	virtual void OnRcv() {}

	virtual void OnError(int errCode) {
		// in case of 'graceful' disconnection error code is 0
	}
};
