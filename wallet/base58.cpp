// Copyright 2018-2019 The Beam Team
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

#include "base58.h"

#include <cmath>
#include <iterator>
#include <algorithm>

using namespace std;
using ByteBuffer = std::vector<uint8_t>;
namespace
{
    uint8_t Divide(ByteBuffer& input, uint32_t fromBase, uint32_t divider)
    {
        uint32_t reminder = 0;
        size_t i = 0;
        // skip leading zeroes
        for (i = 0; i < input.size() && input[i] == 0; ++i);

        // divide
        for (; i < input.size(); ++i)
        {
            auto k = input[i] + reminder * fromBase;
            input[i] = uint8_t(k / divider);
            reminder = k % divider;
        }
        return static_cast<uint8_t>(reminder);
    }

    size_t CountLeadingZeros(const ByteBuffer& input)
    {
        size_t leadingZeros = 0;
        for (size_t i = 0; i < input.size() && input[i] == 0; ++i, ++leadingZeros);
        return leadingZeros;
    }

    ByteBuffer Convert(ByteBuffer input, uint32_t fromBase, uint32_t toBase)
    {
        size_t zeros = CountLeadingZeros(input);
        auto ratio = std::log(fromBase) / std::log(toBase);

        size_t outputSize = static_cast<size_t>(ceil((input.size() - zeros) * ratio));

        ByteBuffer output(outputSize, 0);

        for (auto it = output.rbegin(); it != output.rend(); ++it)
        {
            auto reminder = Divide(input, fromBase, toBase);
            *it = reminder;
        }

        auto outputZeros = CountLeadingZeros(output);

        ByteBuffer paddedOutput(outputSize - outputZeros + zeros, 0);
        copy(output.begin() + outputZeros, output.end(), paddedOutput.begin() + zeros);

        return paddedOutput;
    }

    string ToString(const vector<uint8_t>& indices, std::string_view alphabet)
    {
        string res;
        res.resize(indices.size());

        for (size_t i = 0; i < indices.size(); ++i)
        {
            res[i] = alphabet[indices[i]];
        }
        return res;
    }

    const char* Base58Alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
}

namespace beam::wallet
{
    string EncodeToBase58(const vector<uint8_t>& input)
    {
        return ToString(Convert(input, 256, 58), Base58Alphabet);
    }

    vector<uint8_t> DecodeBase58(const string& input)
    {
        string_view alphabet(Base58Alphabet);
        vector<uint8_t> indices(input.size(), 0);
        for (size_t i = 0; i < input.size(); ++i)
        {
            // alphabet is sorted
            auto it = lower_bound(alphabet.begin(), alphabet.end(), input[i]);
            if (it == alphabet.end() || *it != input[i])
            {
                return {};
            }
            indices[i] = static_cast<uint8_t>(distance(alphabet.begin(), it));
        }
        return Convert(move(indices), 58, 256);
    }
}