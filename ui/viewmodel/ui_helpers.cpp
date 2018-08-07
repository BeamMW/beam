#include "ui_helpers.h"
#include <QDateTime>

using namespace std;
using namespace beam;

namespace beamui
{
    QString toString(const beam::WalletID& walletID)
    {
        auto id = std::to_string(walletID);
        ltrim(id, '0');
        return QString::fromStdString(id);
    }

    QString BeamToString(const Amount& value)
    {
        auto str = std::to_string(double(int64_t(value)) / Rules::Coin);

        str.erase(str.find_last_not_of('0') + 1, std::string::npos);
        str.erase(str.find_last_not_of('.') + 1, std::string::npos);

        return QString::fromStdString(str);
    }

    inline void ltrim(std::string &s, char sym)
    {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [sym](char ch) {return ch != sym; }));
    }

    QString toString(const beam::Timestamp& ts)
    {
        QDateTime datetime;
        datetime.setTime_t(ts);

        return datetime.toString(Qt::SystemLocaleShortDate);
    }

    
}