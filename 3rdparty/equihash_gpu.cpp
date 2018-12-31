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

#include "equihash_gpu.h"
#include "clHost.h"
#include <thread>
#include "utility/logger.h"
#include <random>
#include <mutex>
#include "3rdparty/crypto/equihash.h"

using namespace std;

namespace
{
    class WorkProvider : public beamMiner::minerBridge
    {
    public:
        WorkProvider(beamMiner::clHost& host)
            : _input(nullptr)
            , _sizeInput(0)
            , _valid()
            , _foundSolution(false)
            , _host(host)
        {
            random_device rd;
            default_random_engine generator(rd());
            uniform_int_distribution<uint64_t> distribution(0, 0xFFFFFFFFFFFFFFFF);

            // We pick a random start nonce
            _nonce = distribution(generator);
        }
        virtual ~WorkProvider()
        {}

        void setWork(const void* input, uint32_t sizeInput, const EquihashGpu::IsValid& valid)
        {
            std::unique_lock<std::mutex> guard(_mutex);
            _input = input;
            _sizeInput = sizeInput;
            _valid = valid;

            _foundSolution = false;
        }

        bool _foundSolution;
        std::vector<uint8_t> _compressed;
        beam::Block::PoW::NonceType _foundNonce;

    private: 
        bool hasWork() override
        {
            return !_foundSolution;
        }

        void getWork(int64_t* workOut, uint64_t* nonceOut, uint8_t* dataOut) override
        {
            std::unique_lock<std::mutex> guard(_mutex);
            if (_input == nullptr || _sizeInput == 0)
            {
                return;
            }
            *workOut = 1;
            *nonceOut = _nonce.fetch_add(1);
             
            memcpy(dataOut, _input, _sizeInput);
        }


        void handleSolution(int64_t &workId, uint64_t &nonce, std::vector<uint32_t> &indices) override
        {
            beam::Block::PoW::NonceType t((const uint8_t*)&nonce);
            std::unique_lock<std::mutex> guard(_mutex);

            if (_foundSolution)
                return;

            auto compressed = GetMinimalFromIndices(indices, 25);
 
            if (_valid(compressed, t))
            {
                _foundSolution = true;
                _host.stopMining();
                _compressed = compressed;
                _foundNonce = t;
            }
        }
    public:
        std::mutex _mutex;
    private:
        std::atomic<uint64_t> _nonce;
        
        const void* _input;
        uint32_t _sizeInput;
        EquihashGpu::IsValid _valid;

        beamMiner::clHost& _host;
    };
}

EquihashGpu::EquihashGpu()
    : m_Host(new beamMiner::clHost)
    , m_Bridge(new WorkProvider(*m_Host.get()))
{
    std::vector<int32_t> devices;

    {
        if (devices.size() == 0) devices.assign(1, -1);
        sort(devices.begin(), devices.end());
    }

    m_Host->setup(m_Bridge.get(), devices, false);
}

bool EquihashGpu::solve(const void* input, uint32_t sizeInput, const IsValid& valid, const Cancel& cancel)
{
    WorkProvider& workProvider = (WorkProvider&)(*m_Bridge.get());
    workProvider.setWork(input, sizeInput, valid);
    m_Host->startMining();
    std::unique_lock<std::mutex> guard(workProvider._mutex);
    if (workProvider._foundSolution)
    {
        return valid(workProvider._compressed, workProvider._foundNonce);
    }
    return false;
}
