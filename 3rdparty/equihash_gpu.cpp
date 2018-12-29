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
     //       LOG_DEBUG() << " ===================== getWork() nonce: " << _nonce;

            //memcpy(nonceOut, &_nonce, 8);
          //   _nonce.Export(*nonceOut);
            *nonceOut = _nonce.fetch_add(1);
           //  _nonce.Inc();
             
            memcpy(dataOut, _input, _sizeInput);
        }

        static void CompressArray(const unsigned char* in, size_t in_len,
            unsigned char* out, size_t out_len,
            size_t bit_len, size_t byte_pad) {
            assert(bit_len >= 8);
            assert(8 * sizeof(uint32_t) >= bit_len);

            size_t in_width{ (bit_len + 7) / 8 + byte_pad };
            assert(out_len == (bit_len*in_len / in_width + 7) / 8);

            uint32_t bit_len_mask{ ((uint32_t)1 << bit_len) - 1 };

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

        static inline uint32_t htobe32(uint32_t x)
        {
            return (((x & 0xff000000U) >> 24) | ((x & 0x00ff0000U) >> 8) |
                ((x & 0x0000ff00U) << 8) | ((x & 0x000000ffU) << 24));
        }


#endif // WIN32

        static void EhIndexToArray(const uint32_t i, unsigned char* array) {
            static_assert(sizeof(uint32_t) == 4, "");
            uint32_t bei = htobe32(i);
            memcpy(array, &bei, sizeof(uint32_t));
        }

        static std::vector<unsigned char> GetMinimalFromIndices(std::vector<uint32_t> indices, size_t cBitLen) {
            assert(((cBitLen + 1) + 7) / 8 <= sizeof(uint32_t));
            size_t lenIndices{ indices.size() * sizeof(uint32_t) };
            size_t minLen{ (cBitLen + 1)*lenIndices / (8 * sizeof(uint32_t)) };
            size_t bytePad{ sizeof(uint32_t) - ((cBitLen + 1) + 7) / 8 };
            std::vector<unsigned char> array(lenIndices);
            for (size_t i = 0; i < indices.size(); i++) {
                EhIndexToArray(indices[i], array.data() + (i * sizeof(uint32_t)));
            }
            std::vector<unsigned char> ret(minLen);
            CompressArray(array.data(), lenIndices, ret.data(), minLen, cBitLen + 1, bytePad);
            return ret;
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
                //std::unique_lock<std::mutex> guard(_mutex);
                _foundSolution = true;
                _host.stopMining();
                _compressed = compressed;
                _foundNonce = t;
            }
        }
    public:
        std::mutex _mutex;
    private:
        //beam::Block::PoW::NonceType _nonce;
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
    devices.push_back(2);

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
