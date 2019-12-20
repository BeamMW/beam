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

#pragma once

namespace beam::wallet
{
    struct IVariablesDB
    {
        using Ptr = std::shared_ptr<IVariablesDB>;

        // Set of methods for low level database manipulation
        virtual void setVarRaw(const char* name, const void* data, size_t size) = 0;
        virtual bool getVarRaw(const char* name, void* data, int size) const = 0;
        virtual void removeVarRaw(const char* name) = 0;

        virtual void setPrivateVarRaw(const char* name, const void* data, size_t size) = 0;
        virtual bool getPrivateVarRaw(const char* name, void* data, int size) const = 0;

        // TODO: Consider refactoring
        virtual bool getBlob(const char* name, ByteBuffer& var) const = 0;
    };
}
