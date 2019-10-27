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

#include <vector>
#include <QObject>

class Connections {
public:
    friend Connections& operator<< (Connections& holder, const QMetaObject::Connection& conn) {
        holder._conns.push_back(conn);
        return holder;
    }

    void disconnect() {
        for(const auto& conn: _conns) QObject::disconnect(conn);
        decltype(_conns)().swap(_conns);
    }

    ~Connections() {
        disconnect();
    }

private:
    std::vector<QMetaObject::Connection> _conns;
};

inline auto MakeConnectionPtr() {
    return std::make_shared<QMetaObject::Connection>();
}

inline QString str2qstr(const std::string& str) {
    return QString::fromStdString(str);
}

inline std::string vec2str(const std::vector<std::string>& vec, char separator)
{
    return std::accumulate(
        std::next(vec.begin()), vec.end(), *vec.begin(),
        [separator](std::string a, std::string b)
    {
        return a + separator + b;
    });
}