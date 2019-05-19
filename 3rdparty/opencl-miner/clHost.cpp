// BEAM OpenCL Miner
// OpenCL Host Interface
// Copyright 2018 The Beam Team	
// Copyright 2018 Wilke Trei

#include "clHost.h"
#include <thread>
#include <iostream>
#include <cstdlib>
#include <string>
#include <sstream>
#include <iomanip>

#include "utility/logger.h"

namespace beamMiner {

using namespace std;

// Helper functions to split a string
inline vector<string> &split(const string &s, char delim, vector<string> &elems) {
    stringstream ss(s);
    string item;
    while(getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}


inline vector<string> split(const string &s, char delim) {
    vector<string> elems;
    return split(s, delim, elems);
}


// Helper function that tests if a OpenCL device supports a certain CL extension
inline bool hasExtension(cl::Device &device, string extension) {
	string info;
	device.getInfo(CL_DEVICE_EXTENSIONS, &info);
	vector<string> extens = split(info, ' ');

	for (uint32_t i=0; i<extens.size(); i++) {
		if (extens[i].compare(extension) == 0) 	return true;
	} 
	return false;
}


// This is a bit ugly c-style, but the OpenCL headers are initially for c and
// support c-style callback functions (no member functions) only.
// This function will be called every time a GPU is done with its current work
void CL_CALLBACK CCallbackFunc(cl_event ev, cl_int err , void* data) {
	clHost* self = static_cast<clHost*>(((clCallbackData*) data)->host);
	self->callbackFunc(err,data);
}


// Function to load the OpenCL kernel and prepare our device for mining
void clHost::loadAndCompileKernel(cl::Device &device, uint32_t pl) {
	LOG_INFO() << "Loading and compiling Beam OpenCL Kernel";

	// reading the kernel file
	cl_int err;

    static const char equihash_150_5_cl[] =
    {
        #include "equihash_150_5.dat"
        , '\0'
    };

	cl::Program::Sources source(1,std::make_pair(equihash_150_5_cl, sizeof(equihash_150_5_cl)));

	// Create a program object and build it
	vector<cl::Device> devicesTMP;
	devicesTMP.push_back(device);

	cl::Program program(contexts[pl], source);
	err = program.build(devicesTMP,"");

	// Check if the build was Ok
	if (!err) {
		LOG_INFO() << "Build sucessfull. ";

		// Store the device and create a queue for it
		cl_command_queue_properties queue_prop = 0;  
		devices.push_back(device);
		queues.push_back(cl::CommandQueue(contexts[pl], devices[devices.size()-1], queue_prop, NULL)); 

		// Reserve events, space for storing results and so on
		events.push_back(cl::Event());
		results.push_back(NULL);
		currentWork.push_back(clCallbackData());
		paused.push_back(true);
		solutionCnt.push_back(0);

		// Create the kernels
		vector<cl::Kernel> newKernels;	
		newKernels.push_back(cl::Kernel(program, "clearCounter", &err));
		newKernels.push_back(cl::Kernel(program, "round0", &err));
		newKernels.push_back(cl::Kernel(program, "round1", &err));
		newKernels.push_back(cl::Kernel(program, "round2", &err));
		newKernels.push_back(cl::Kernel(program, "round3", &err));
		newKernels.push_back(cl::Kernel(program, "round4", &err));
		newKernels.push_back(cl::Kernel(program, "round5", &err));
		newKernels.push_back(cl::Kernel(program, "combine", &err));
		kernels.push_back(newKernels);

		// Create the buffers
		vector<cl::Buffer> newBuffers;	
		newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint4) * 71303168, NULL, &err));
		newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint4) * 71303168, NULL, &err)); 
		newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint4) * 71303168, NULL, &err)); 
		newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint2) * 71303168, NULL, &err)); 
		newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint4) * 256, NULL, &err));   
		newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint) * 49152, NULL, &err));  
		newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint) * 324, NULL, &err));  
		buffers.push_back(newBuffers);		
			
	} else {
		LOG_INFO() << "Program build error, device will not be used. ";
		// Print error msg so we can debug the kernel source
		LOG_INFO() << "Build Log: "     << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(devicesTMP[0]);
	}
}


// Detect the OpenCL hardware on this system
void clHost::detectPlatFormDevices(vector<int32_t> selDev, bool allowCPU) {
	// read the OpenCL platforms on this system
	cl::Platform::get(&platforms);  

	// this is for enumerating the devices
	int32_t curDiv = 0;
	uint32_t selNum = 0;
	
	for (uint32_t pl=0; pl<platforms.size(); pl++) {
		// Create the OpenCL contexts, one for each platform
		cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platforms[pl](), 0};  
		cl::Context context;
		if (allowCPU) { 
			context = cl::Context(CL_DEVICE_TYPE_ALL, properties);
		} else {
			context = cl::Context(CL_DEVICE_TYPE_GPU, properties);
		}
		contexts.push_back(context);

		// Read the devices of this platform
		vector< cl::Device > nDev = context.getInfo<CL_CONTEXT_DEVICES>();  
		for (uint32_t di=0; di<nDev.size(); di++) {
			
			// Print the device name
			string name;
			if ( hasExtension(nDev[di], "cl_amd_device_attribute_query") ) {
				nDev[di].getInfo(0x4038,&name);			// on AMD this gives more readable result
			} else {
				nDev[di].getInfo(CL_DEVICE_NAME, &name); 	// for all other GPUs
			}

			// Get rid of strange characters at the end of device name
			if (isalnum((int) name.back()) == 0) {
				name.pop_back();
			} 
			
			LOG_INFO() << "Found device " << curDiv << ": " << name;

			// Check if the device should be selected
			bool pick = false;
			if (selDev[0] == -1) pick = true;
			if (selNum < selDev.size()) {
				if (curDiv == selDev[selNum]) {
					pick = true;
					selNum++;
				}				
			}
			

			if (pick) {
				// Check if the CPU / GPU has enough memory
				uint64_t deviceMemory = nDev[di].getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
				uint64_t needed = 7* ((uint64_t) 570425344) + 4096 + 196608 + 1296;

				if (deviceMemory > needed) {
					LOG_INFO() << "Memory check ok";
					loadAndCompileKernel(nDev[di], pl);
				} else {
					LOG_INFO() << "Memory check failed";
					LOG_INFO() << "Device reported " << deviceMemory / (1024*1024) << "MByte memory, " << needed/(1024*1024) << " are required ";
				}
			} else {
				LOG_INFO() << "Device will not be used, it was not included in --devices parameter.";
			}

			curDiv++; 
		}
	}
}


// Setup function called from outside
void clHost::setup(minerBridge* br, vector<int32_t> devSel,  bool allowCPU) {
	bridge = br;
	detectPlatFormDevices(devSel, allowCPU);
}


// Function that will catch new work from the stratum interface and then queue the work on the device
void clHost::queueKernels(uint32_t gpuIndex, clCallbackData* workData) {
	int64_t id;
    uint32_t difficulty;
	cl_ulong4 work;	
	cl_ulong nonce;

	// Get a new set of work from the stratum interface
	bridge->getWork(&id,(uint64_t *) &nonce, (uint8_t *) &work, &difficulty);

	workData->workId = id;
	workData->nonce = (uint64_t) nonce;
    workData->difficulty = difficulty;


	// Kernel arguments for cleanCounter
	kernels[gpuIndex][0].setArg(0, buffers[gpuIndex][5]); 
	kernels[gpuIndex][0].setArg(1, buffers[gpuIndex][6]);

	// Kernel arguments for round0
	kernels[gpuIndex][1].setArg(0, buffers[gpuIndex][0]); 
	kernels[gpuIndex][1].setArg(1, buffers[gpuIndex][2]); 
	kernels[gpuIndex][1].setArg(2, buffers[gpuIndex][5]); 
	kernels[gpuIndex][1].setArg(3, work); 
	kernels[gpuIndex][1].setArg(4, nonce); 

	// Kernel arguments for round1
	kernels[gpuIndex][2].setArg(0, buffers[gpuIndex][0]); 
	kernels[gpuIndex][2].setArg(1, buffers[gpuIndex][2]); 
	kernels[gpuIndex][2].setArg(2, buffers[gpuIndex][1]); 
	kernels[gpuIndex][2].setArg(3, buffers[gpuIndex][3]); 	// Index tree will be stored here
	kernels[gpuIndex][2].setArg(4, buffers[gpuIndex][5]); 

	// Kernel arguments for round2
	kernels[gpuIndex][3].setArg(0, buffers[gpuIndex][1]); 
	kernels[gpuIndex][3].setArg(1, buffers[gpuIndex][0]);	// Index tree will be stored here 
	kernels[gpuIndex][3].setArg(2, buffers[gpuIndex][5]); 

	// Kernel arguments for round3
	kernels[gpuIndex][4].setArg(0, buffers[gpuIndex][0]); 
	kernels[gpuIndex][4].setArg(1, buffers[gpuIndex][1]); 	// Index tree will be stored here 
	kernels[gpuIndex][4].setArg(2, buffers[gpuIndex][5]); 

	// Kernel arguments for round4
	kernels[gpuIndex][5].setArg(0, buffers[gpuIndex][1]); 
	kernels[gpuIndex][5].setArg(1, buffers[gpuIndex][2]); 	// Index tree will be stored here 
	kernels[gpuIndex][5].setArg(2, buffers[gpuIndex][5]);  

	// Kernel arguments for round5
	kernels[gpuIndex][6].setArg(0, buffers[gpuIndex][2]); 
	kernels[gpuIndex][6].setArg(1, buffers[gpuIndex][4]); 	// Index tree will be stored here 
	kernels[gpuIndex][6].setArg(2, buffers[gpuIndex][5]);  

	// Kernel arguments for Combine
	kernels[gpuIndex][7].setArg(0, buffers[gpuIndex][0]); 
	kernels[gpuIndex][7].setArg(1, buffers[gpuIndex][1]); 	
	kernels[gpuIndex][7].setArg(2, buffers[gpuIndex][2]); 
	kernels[gpuIndex][7].setArg(3, buffers[gpuIndex][3]); 	
	kernels[gpuIndex][7].setArg(4, buffers[gpuIndex][4]); 
	kernels[gpuIndex][7].setArg(5, buffers[gpuIndex][5]); 	
	kernels[gpuIndex][7].setArg(6, buffers[gpuIndex][6]);

	// Queue the kernels
	queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][0], cl::NDRange(0), cl::NDRange(12288), cl::NDRange(256), NULL, NULL);
	queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][1], cl::NDRange(0), cl::NDRange(22369536), cl::NDRange(256), NULL, NULL);
	queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][2], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
	queues[gpuIndex].flush();
	queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][3], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
	queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][4], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
	queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][5], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
	queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][6], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
	queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][7], cl::NDRange(0), cl::NDRange(4096), cl::NDRange(16), NULL, NULL);
}


// this function will sumit the solutions done on GPU, then fetch new work and restart mining
void clHost::callbackFunc(cl_int err , void* data){
	clCallbackData* workInfo = (clCallbackData*) data;
	uint32_t gpu = workInfo->gpuIndex;

	// Read the number of solutions of the last iteration
	uint32_t solutions = results[gpu][0];
	for (uint32_t  i=0; i<solutions; i++) {
		vector<uint32_t> indexes;
		indexes.assign(32,0);
		memcpy(indexes.data(), &results[gpu][4 + 32*i], sizeof(uint32_t) * 32);

		bridge->handleSolution(workInfo->workId,workInfo->nonce,indexes, workInfo->difficulty);
	}

	solutionCnt[gpu] += solutions;

	// Get new work and resume working
	if (bridge->hasWork() && restart) {
		queues[gpu].enqueueUnmapMemObject(buffers[gpu][6], results[gpu], NULL, NULL);
		queueKernels(gpu, &currentWork[gpu]);
		results[gpu] = (unsigned *) queues[gpu].enqueueMapBuffer(buffers[gpu][6], CL_FALSE, CL_MAP_READ, 0, sizeof(cl_uint4) * 81, NULL, &events[gpu], NULL);
		events[gpu].setCallback(CL_COMPLETE, &CCallbackFunc, (void*) &currentWork[gpu]);
		queues[gpu].flush();
	} else {
		paused[gpu] = true;
		LOG_INFO() << "Device will be paused, waiting for new work";
	}
}

void clHost::stopMining()
{
    restart = false;
}

void clHost::startMining()
{
    restart = true;
	// Start mining initially
	for (uint32_t i=0; i<devices.size() && restart; i++) {
		paused[i] = false;

		currentWork[i].gpuIndex = i;
		currentWork[i].host = (void*) this;
		queueKernels(i, &currentWork[i]);

		results[i] = (unsigned *) queues[i].enqueueMapBuffer(buffers[i][6], CL_FALSE, CL_MAP_READ, 0, sizeof(cl_uint4) * 81, NULL, &events[i], NULL);
		events[i].setCallback(CL_COMPLETE, &CCallbackFunc, (void*) &currentWork[i]);
		queues[i].flush();
	}

	// While the mining is running print some statistics
	while (restart) {
		this_thread::sleep_for(std::chrono::seconds(15));

		// Print performance stats (roughly)
        stringstream ss;
		
        for (uint32_t i = 0; i < devices.size(); i++) {
            uint32_t sol = solutionCnt[i];
            solutionCnt[i] = 0;
            ss << fixed << setprecision(2) << (double)sol / 15.0 << " sol/s ";
        }
        LOG_INFO() << "Performance: " << ss.str();

		// Check if there are paused devices and restart them
		for (uint32_t i=0; i<devices.size(); i++) {
			if (paused[i] && bridge->hasWork() && restart) {
				paused[i] = false;
				
				// Same as above
				queueKernels(i, &currentWork[i]);

				results[i] = (unsigned *) queues[i].enqueueMapBuffer(buffers[i][6], CL_FALSE, CL_MAP_READ, 0, sizeof(cl_uint4) * 81, NULL, &events[i], NULL);
				events[i].setCallback(CL_COMPLETE, &CCallbackFunc, (void*) &currentWork[i]);
				queues[i].flush();
			}

		}
	}

    cl::Event::waitForEvents(events);
    LOG_DEBUG() << "Miner stopped";
}

clHost::~clHost()
{
    LOG_INFO() << "clHost::~clHost()";
}

} 	// end namespace



