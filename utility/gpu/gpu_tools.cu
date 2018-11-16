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

#include "gpu_tools.h"

#include "cuda_runtime.h"

namespace beam
{
bool HasSupportedCard()
{
    int device;
    cudaError_t code = cudaGetDevice(&device);
    if (code == cudaSuccess)
        return true;

    return false;
}
}