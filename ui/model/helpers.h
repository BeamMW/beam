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
private:
    std::vector<QMetaObject::Connection> _conns;
};

inline auto MakeConnectionPtr() {
    return std::make_shared<QMetaObject::Connection>();
}
