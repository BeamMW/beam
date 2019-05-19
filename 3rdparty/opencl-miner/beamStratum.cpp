// BEAM OpenCL Miner
// Stratum interface class
// Copyright 2018 The Beam Team	
// Copyright 2018 Wilke Trei


#include "beamStratum.h"
#include "crypto/sha256.c"

namespace beamMiner {

// This one ensures that the calling thread can work on immediately
void beamStratum::queueDataSend(string data) {
	io_service.post(boost::bind(&beamStratum::syncSend,this, data)); 
}

// Function to add a string into the socket write queue
void beamStratum::syncSend(string data) {
	writeRequests.push_back(data);
	activateWrite();
}


// Got granted we can write to our connection, lets do so	
void beamStratum::activateWrite() {
	if (!activeWrite && writeRequests.size() > 0) {
		activeWrite = true;

		string json = writeRequests.front();
		writeRequests.pop_front();

		std::ostream os(&requestBuffer);
		os << json;
		if (debug) cout << "Write to connection: " << json;

		boost::asio::async_write(*socket, requestBuffer, boost::bind(&beamStratum::writeHandler,this, boost::asio::placeholders::error)); 		
	}
}
	

// Once written check if there is more to write
void beamStratum::writeHandler(const boost::system::error_code& err) {
	activeWrite = false;
	activateWrite(); 
	if (err) {
		if (debug) cout << "Write to stratum failed: " << err.message() << endl;
	} 
}


// Called by main() function, starts the stratum client thread
void beamStratum::startWorking(){
	std::thread (&beamStratum::connect,this).detach();
}

// This function will be used to establish a connection to the API server
void beamStratum::connect() {	
	while (true) {
		tcp::resolver::query q(host, port); 

		cout << "Connecting to " << host << ":" << port << endl;
		try {
	    		tcp::resolver::iterator endpoint_iterator = res.resolve(q);
			tcp::endpoint endpoint = *endpoint_iterator;
			socket.reset(new boost::asio::ssl::stream<tcp::socket>(io_service, context));

			socket->set_verify_mode(boost::asio::ssl::verify_none);
    			socket->set_verify_callback(boost::bind(&beamStratum::verifyCertificate, this, _1, _2));

			socket->lowest_layer().async_connect(endpoint,
			boost::bind(&beamStratum::handleConnect, this, boost::asio::placeholders::error, ++endpoint_iterator));	

			io_service.run();
		} catch (std::exception const& _e) {
			 cout << "Stratum error: " <<  _e.what() << endl;
		}

		workId = -1;
		io_service.reset();
		socket->lowest_layer().close();

		cout << "Lost connection to API server" << endl;
		cout << "Trying to connect in 5 seconds"<< endl;

		std::this_thread::sleep_for(std::chrono::seconds(5));
	}		
}


// Once the physical connection is there start a TLS handshake
void beamStratum::handleConnect(const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator) {
	if (!err) {
	cout << "Connected to node. Starting TLS handshake." << endl;

      	// The connection was successful. Do the TLS handshake
	socket->async_handshake(boost::asio::ssl::stream_base::client,boost::bind(&beamStratum::handleHandshake, this, boost::asio::placeholders::error));
	
    	} else if (err != boost::asio::error::operation_aborted) {
		if (endpoint_iterator != tcp::resolver::iterator()) {
			// The endpoint did not work, but we can try the next one
			tcp::endpoint endpoint = *endpoint_iterator;

			socket->lowest_layer().async_connect(endpoint,
			boost::bind(&beamStratum::handleConnect, this, boost::asio::placeholders::error, ++endpoint_iterator));
		} 
	} 	
}


// Dummy function: we will not verify if the endpoint is verified at the moment,
// still there is a TLS handshake, so connection is encrypted
bool beamStratum::verifyCertificate(bool preverified, boost::asio::ssl::verify_context& ctx){
	return true;
}


void beamStratum::handleHandshake(const boost::system::error_code& error) {
	if (!error) {
		// Listen to receive stratum input
		boost::asio::async_read_until(*socket, responseBuffer, "\n",
		boost::bind(&beamStratum::readStratum, this, boost::asio::placeholders::error));

		cout << "TLS Handshake sucess" << endl;
		
		// The connection was successful. Send the login request
		std::stringstream json;
		json << "{\"method\":\"login\", \"api_key\":\"" << apiKey << "\", \"id\":\"login\",\"jsonrpc\":\"2.0\"} \n";
		queueDataSend(json.str());	
	} else {
		cout << "Handshake failed: " << error.message() << "\n";
	}
}


// Simple helper function that casts a hex string into byte array
vector<uint8_t> parseHex (string input) {
	vector<uint8_t> result ;
	result.reserve(input.length() / 2);
	for (uint32_t i = 0; i < input.length(); i += 2){
		uint32_t byte;
		std::istringstream hex_byte(input.substr(i, 2));
		hex_byte >> std::hex >> byte;
		result.push_back(static_cast<unsigned char>(byte));
	}
	return result;
}


// Main stratum read function, will be called on every received data
void beamStratum::readStratum(const boost::system::error_code& err) {
	if (!err) {
		// We just read something without problem.
		std::istream is(&responseBuffer);
		std::string response;
		getline(is, response);

		if (debug) cout << "Incoming Stratum: " << response << endl;

		// Parse the input to a property tree
		pt::iptree jsonTree;
		try {
			istringstream jsonStream(response);
			pt::read_json(jsonStream,jsonTree);

			// This should be for any valid stratum
			if (jsonTree.count("method") > 0) {	
				string method = jsonTree.get<string>("method");
			
				// Result to a node request
				if (method.compare("result") == 0) {
					// A login reply
					if (jsonTree.get<string>("id").compare("login") == 0) {
						int32_t code = jsonTree.get<int32_t>("code");
						if (code >= 0) {
							cout << "Login at node accepted \n" << endl;
						} else {
							cout << "Error: Login at node not accepted. Closing miner." << endl;
							exit(0);
						}	
					} else {	// A share reply
						int32_t code = jsonTree.get<int32_t>("code");
						if (code == 1) {
							cout << "Solution for work id " << jsonTree.get<string>("id") << "accepted" << endl;
						} else {
							cout << "Warning: Solution for work id " << jsonTree.get<string>("id") << "not accepted" << endl;
						}
					}
				}

				// A new job decription;
				if (method.compare("job") == 0) {
					updateMutex.lock();
					// Get new work load
					string work = jsonTree.get<string>("input");
					serverWork = parseHex(work);

					// Get jobId of new job
					workId =  jsonTree.get<uint64_t>("id");	
					
					// Get the target difficulty
					uint32_t stratDiff =  jsonTree.get<uint32_t>("difficulty");
					powDiff = beam::Difficulty(stratDiff);
					updateMutex.unlock();	

					cout << "New work received with id " << workId << " at difficulty " << powDiff.ToFloat() << endl;	
				}

				// Cancel a running job
				if (method.compare("cancel") == 0) {
					updateMutex.lock();
					// Get jobId of canceled job
					uint64_t id =  jsonTree.get<uint64_t>("id");
					// Set it to an unlikely value;
					if (id == (uint64_t)workId) workId = -1;
					updateMutex.unlock();
				}
			}

			

		} catch(const pt::ptree_error &e) {
			cout << "Json parse error: " << e.what() << endl; 
		}

		// Prepare to continue reading
		boost::asio::async_read_until(*socket, responseBuffer, "\n",
        	boost::bind(&beamStratum::readStratum, this, boost::asio::placeholders::error));
	}
}


// Checking if we have valid work, else the GPUs will pause
bool beamStratum::hasWork() {
	return (workId >= 0);
}


// function the clHost class uses to fetch new work
void beamStratum::getWork(int64_t* workOut, uint64_t* nonceOut, uint8_t* dataOut, uint32_t*) {
	*workOut = workId;

	// nonce is atomic, so every time we call this will get a nonce increased by one
	*nonceOut = nonce.fetch_add(1);
	
	memcpy(dataOut, serverWork.data(), 32);
}


void CompressArray(const unsigned char* in, size_t in_len,
                   unsigned char* out, size_t out_len,
                   size_t bit_len, size_t byte_pad) {
	assert(bit_len >= 8);
	assert(8*sizeof(uint32_t) >= bit_len);

	size_t in_width { (bit_len+7)/8 + byte_pad };
	assert(out_len == (bit_len*in_len/in_width + 7)/8);

	uint32_t bit_len_mask { ((uint32_t)1 << bit_len) - 1 };

	// The acc_bits least-significant bits of acc_value represent a bit sequence
	// in big-endian order.
	size_t acc_bits = 0;
	uint32_t acc_value = 0;

	size_t j = 0;
	for (size_t i = 0; i < out_len; i++) {
		// When we have fewer than 8 bits left in the accumulator, read the next
		// input element.
		if (acc_bits < 8) {
			if (j < in_len) {
				acc_value = acc_value << bit_len;
				for (size_t x = byte_pad; x < in_width; x++) {
					acc_value = acc_value | (
					(
					// Apply bit_len_mask across byte boundaries
					in[j + x] & ((bit_len_mask >> (8 * (in_width - x - 1))) & 0xFF)
					) << (8 * (in_width - x - 1))); // Big-endian
				}
				j += in_width;
				acc_bits += bit_len;
			}
			else {
				acc_value <<= 8 - acc_bits;
				acc_bits += 8 - acc_bits;;
			}
		}

		acc_bits -= 8;
		out[i] = (acc_value >> acc_bits) & 0xFF;
	}
}

#ifdef WIN32

inline uint32_t htobe32(uint32_t x)
{
    return (((x & 0xff000000U) >> 24) | ((x & 0x00ff0000U) >> 8) |
        ((x & 0x0000ff00U) << 8) | ((x & 0x000000ffU) << 24));
}


#endif // WIN32

// Big-endian so that lexicographic array comparison is equivalent to integer comparison
void EhIndexToArray(const uint32_t i, unsigned char* array) {
	static_assert(sizeof(uint32_t) == 4, "");
	uint32_t bei = htobe32(i);
	memcpy(array, &bei, sizeof(uint32_t));
}


// Helper function that compresses the solution from 32 unsigned integers (128 bytes) to 104 bytes
std::vector<unsigned char> GetMinimalFromIndices(std::vector<uint32_t> indices, size_t cBitLen) {
	assert(((cBitLen+1)+7)/8 <= sizeof(uint32_t));
	size_t lenIndices { indices.size()*sizeof(uint32_t) };
	size_t minLen { (cBitLen+1)*lenIndices/(8*sizeof(uint32_t)) };
	size_t bytePad { sizeof(uint32_t) - ((cBitLen+1)+7)/8 };
	std::vector<unsigned char> array(lenIndices);
	for (size_t i = 0; i < indices.size(); i++) {
		EhIndexToArray(indices[i], array.data()+(i*sizeof(uint32_t)));
	}
	std::vector<unsigned char> ret(minLen);
	CompressArray(array.data(), lenIndices, ret.data(), minLen, cBitLen+1, bytePad);
	return ret;
}


void beamStratum::testAndSubmit(int64_t wId, uint64_t nonceIn, vector<uint32_t> indices) {
	// First check if the work fits the current work

	if (wId == workId) {	

		// get the compressed representation of the solution and check against target
		vector<uint8_t> compressed;
		compressed = GetMinimalFromIndices(indices,25);

		beam::uintBig_t<32> hv;
		Sha256_Onestep(compressed.data(), compressed.size(), hv.m_pData);

		if (powDiff.IsTargetReached(hv)) {	
	
			// The solutions target is low enough, lets submit it
			vector<uint8_t> nonceBytes;
			
			nonceBytes.assign(8,0);
			*((uint64_t*) nonceBytes.data()) = nonceIn;

			stringstream nonceHex;
			for (int c=0; c<nonceBytes.size(); c++) {
				nonceHex << std::setfill('0') << std::setw(2) << std::hex << (unsigned) nonceBytes[c];
			}

			stringstream solutionHex;
			for (int c=0; c<compressed.size(); c++) {
				solutionHex << std::setfill('0') << std::setw(2) << std::hex << (unsigned) compressed[c];
			}	
			
			// Line the stratum msg up
			std::stringstream json;
			json << "{\"method\" : \"solution\", \"id\": \"" << wId << "\", \"nonce\": \"" << nonceHex.str() 
			     << "\", \"output\": \"" << solutionHex.str() << "\", \"jsonrpc\":\"2.0\" } \n";

			queueDataSend(json.str());	

			cout << "Submitting solution to job " << wId << " with nonce " <<  nonceHex.str() << endl;

		}
	}
}


// Will be called by clHost class for check & submit
void beamStratum::handleSolution(int64_t &workIdVar, uint64_t &nonceVar, vector<uint32_t> &indices, uint32_t) {
	std::thread (&beamStratum::testAndSubmit,this, workIdVar, nonceVar,indices).detach();
}


beamStratum::beamStratum(string hostIn, string portIn, string apiKeyIn, bool debugIn) : res(io_service), context(boost::asio::ssl::context::sslv23)  {
	host = hostIn;
	port = portIn;
	apiKey = apiKeyIn;
	debug = debugIn;

	// Assign the work field and nonce
	serverWork.assign(32,(uint8_t) 0);

	random_device rd;
	default_random_engine generator(rd());
	uniform_int_distribution<uint64_t> distribution(0,0xFFFFFFFFFFFFFFFF);

	// We pick a random start nonce
	nonce = distribution(generator);

	// No work in the beginning
	workId = -1;
}

} // End namespace beamMiner

