// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <CL/cl.hpp>

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

#include <vector>
#include <sstream>

#include "utility/logger.h"
#include "utility/string_helpers.h"

using namespace std;

// TODO: copy-paste from clHost.cpp, pls unify

namespace beam
{
    // Helper function that tests if a OpenCL device supports a certain CL extension
    inline bool hasExtension(cl::Device &device, string extension) 
    {
        string info;
        device.getInfo(CL_DEVICE_EXTENSIONS, &info);
        vector<string> extens = string_helpers::split(info, ' ');

        for (int i = 0; i < extens.size(); i++) {
            if (extens[i].compare(extension) == 0) 	return true;
        }
        return false;
    }

    bool HasSupportedCard()
    {
        static struct 
        {
            bool checked;
            bool supported;
        } status = {false, false};

        if (status.checked)
        {
            return status.supported;
        }

        status.checked = true;

        vector<cl::Platform> platforms;

        cl::Platform::get(&platforms);

        int32_t curDiv = 0;

        for (int pl = 0; pl < platforms.size(); pl++)
        {
            cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platforms[pl](), 0 };

            cl::Context context = cl::Context(CL_DEVICE_TYPE_GPU, properties);

            vector< cl::Device > devices = context.getInfo<CL_CONTEXT_DEVICES>();

            for (int di = 0; di < devices.size(); di++)
            {
                string name;
                if (hasExtension(devices[di], "cl_amd_device_attribute_query")) 
                {
                    devices[di].getInfo(0x4038, &name);			// on AMD this gives more readable result
                }
                else {
                    devices[di].getInfo(CL_DEVICE_NAME, &name); 	// for all other GPUs
                }

                // Get rid of strange characters at the end of device name
                if (isalnum((int)name.back()) == 0) 
                {
                    name.pop_back();
                }

                LOG_INFO() << "Found device " << curDiv << ": " << name;

                {
                    // Check if the CPU / GPU has enough memory
                    uint64_t deviceMemory = devices[di].getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
                    uint64_t needed = 7 * ((uint64_t)570425344) + 4096 + 196608 + 1296;

                    status.supported = deviceMemory > needed;

                    if (status.supported)
                    {
                        LOG_INFO() << "Memory check ok";
                    }
                    else 
                    {
                        LOG_INFO() << "Memory check failed";
                        LOG_INFO() << "Device reported " << deviceMemory / (1024 * 1024) << "MByte memory, " << needed / (1024 * 1024) << " are required ";
                    }
                }
            }
        }

        return status.supported;
    }
}
