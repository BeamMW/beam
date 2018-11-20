#include "ui_helpers.h"

#include <QDateTime>
#include <QLocale>
#include <numeric>

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
        auto real_amount = double(int64_t(value)) / Rules::Coin;
        QString qstr = QLocale().toString(real_amount, 'f', QLocale::FloatingPointShortest);

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
    

    Filter::Filter(size_t size)
        : _samples(size, 0.0)
        , _index{0}
    {
    }
    
    void Filter::addSample(double value)
    {
        _samples[_index] = value;
        _index = (_index + 1) % _samples.size();
    }

    double Filter::getAverage() const
    {
        double sum = accumulate(_samples.begin(), _samples.end(), 0.0);
        return sum / _samples.size();
    }
}