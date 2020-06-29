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
#include "pipe.h"

#include "utility/logger.h"
#include <cstdio>

namespace beam::wallet {
     const int Pipe::SyncFileDescriptor      = 3;
     const int Pipe::HeartbeatFileDescriptor = 4;
     const int Pipe::HeartbeatInterval       = 5000; // every 5 seconds

    Pipe::Pipe(int fd):
        _fd(fd)
    {
        _file = fdopen(_fd, "w");
        if (_file == nullptr)
        {
            LOG_ERROR() << "Failed to open sync pipe";
        }
        else
        {
            LOG_INFO() << "Sync pipe " << _fd << " opened successfully";
        }
    }

    Pipe::~Pipe()
    {
        if (_file)
        {
            fclose(_file);
        }
    }

    void Pipe::notify(const std::string& message) const
    {
        if (_file == nullptr)
        {
            LOG_WARNING() << "Null sync pipe " << _fd << " for message: " << message;
            return;
        }

        const auto chars    = message.size();
        const auto charSize = sizeof(std::string::value_type);
        const auto wsize    = chars * charSize;

        if (wsize != fwrite(message.c_str(), charSize, chars, _file))
        {
            LOG_ERROR() << "Failed to write sync pipe, message: " << message;
        }
        else
        {
            fflush(_file);
            LOG_DEBUG() << "Sync pipe " << _fd << ": " << message << ", " << wsize << " bytes";
        }
    }

    void Pipe::notifyFailed() const
    {
        notify("FAILED");
    }

    void Pipe::notifyAlive() const
    {
        notify("ALIVE");
    }

    void Pipe::notifyListening() const
    {
        notify("LISTENING");
    }
}



