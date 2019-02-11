// BEAM OpenCL Miner
// Stratum interface class
// Copyright 2018 The Beam Team	
// Copyright 2018 Wilke Trei
#pragma once
#include "minerBridge.h"

#include <iostream>
#include <thread>
#include <cstdlib>
#include <string>
#include <sstream>
#include <deque>
#include <random>

#include <boost/scoped_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "core/difficulty.h"
#include "core/uintBig.h"

using namespace std;
using namespace boost::asio;
using boost::asio::ip::tcp;
namespace pt = boost::property_tree;

namespace beamMiner {

#ifndef beamMiner_H 
#define beamMiner_H 

class beamStratum : public minerBridge {
	private:

	// Definitions belonging to the physical connection
	boost::asio::io_service io_service;
	boost::scoped_ptr< boost::asio::ssl::stream<tcp::socket> > socket;
	tcp::resolver res;
	boost::asio::streambuf requestBuffer;
	boost::asio::streambuf responseBuffer;
	boost::asio::ssl::context context;

	// User Data
	string host;
	string port;
	string apiKey;
	bool debug = true;

	// Storage for received work
	int64_t workId;
	std::vector<uint8_t> serverWork;
	std::atomic<uint64_t> nonce;
	beam::Difficulty powDiff;

	//Stratum sending subsystem
	bool activeWrite = false;
	void queueDataSend(string);
	void syncSend(string);
	void activateWrite();
	void writeHandler(const boost::system::error_code&);	
	std::deque<string> writeRequests;

	// Stratum receiving subsystem
	void readStratum(const boost::system::error_code&);
	boost::mutex updateMutex;

	// Connection handling
	void connect();
	void handleConnect(const boost::system::error_code& err,  tcp::resolver::iterator);
	void handleHandshake(const boost::system::error_code& err);
	bool verifyCertificate(bool,boost::asio::ssl::verify_context& );

	// Solution Check & Submit
	void testAndSubmit(int64_t, uint64_t, std::vector<uint32_t>);

	public:
    beamStratum(string, string, string, bool);
	void startWorking();

	bool hasWork() override;
	void getWork(int64_t*, uint64_t*, uint8_t*, uint32_t*) override;

	void handleSolution(int64_t&, uint64_t&, std::vector<uint32_t>&, uint32_t) override;
	
};

#endif 

}

