// BEAM OpenCL Miner
// OpenCL Host Interface
// Copyright 2018 The Beam Team	
// Copyright 2018 Wilke Trei
#pragma once
#include "minerBridge.h"

#include <CL/cl.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>
#include <cstdlib>
#include <climits>

namespace beamMiner {

using std::vector;

struct clCallbackData {
	void* host;
	uint32_t gpuIndex;
	int64_t workId;
	uint64_t nonce;
};

class clHost {
	private:
	// OpenCL 
	vector<cl::Platform> platforms;  
	vector<cl::Context> contexts;
	vector<cl::CommandQueue> queues;
	vector<cl::Device> devices;
	vector<cl::Event> events;
	vector<unsigned*> results;

	vector< vector<cl::Buffer> > buffers;
	vector< vector<cl::Kernel> > kernels;

	// Statistics
	vector<int> solutionCnt;

	// To check if a mining thread stoped and we must resume it
	vector<bool> paused;

	// Callback data
	vector<clCallbackData> currentWork;
	bool restart = true;


	// Functions
	void detectPlatFormDevices(vector<int32_t>, bool);
	void loadAndCompileKernel(cl::Device &, uint32_t);
	void queueKernels(uint32_t, clCallbackData*);
	
	// The connector
	minerBridge* bridge;

	public:
	
	void setup(minerBridge*, vector<int32_t>, bool);
	void startMining();	
	void callbackFunc(cl_int, void*);
};

}
