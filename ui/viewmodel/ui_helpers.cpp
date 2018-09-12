#include "ui_helpers.h"

#include <QDateTime>
#include <QLocale>

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
        constexpr int kMaxNumberOfSignificantDigits = 12;

        auto real_amount = double(int64_t(value)) / Rules::Coin;
        QString qstr = QLocale().toString(real_amount, 'g', kMaxNumberOfSignificantDigits);
        return qstr;
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