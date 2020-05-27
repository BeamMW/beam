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

#include <string>

namespace beam::wallet {
    class Pipe
    {
        int _fd = 0;
        FILE *_file = nullptr;
    public:
        explicit Pipe(int fd);
        ~Pipe();

        void notify(const std::string &message) const;
        void notifyFailed() const;
        void notifyAlive()  const;
        void notifyListening() const;

        static const int SyncFileDescriptor;
        static const int HeartbeatFileDescriptor;
        static const int HeartbeatInterval;
    };
}
