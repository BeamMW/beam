// Copyright 2020 The Beam Team
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
#pragma once

#include <cstddef>
#include <vector>

namespace beam::wallet
{

class Filter
{
public:
    Filter(size_t size = 12);
    void addSample(double value);
    double getAverage() const;
    double getMedian() const;
private:
    std::vector<double> _samples;
    size_t _index;
    bool _is_poor;
};
}  // namespace beamui
