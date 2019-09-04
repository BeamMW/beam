#pragma once
#include <QObject>
#include "wallet/common.h"

namespace beamui
{
    enum class Currencies
    {
        Beam,
        Bitcoin,
        Litecoin,
        Qtum,
        Unknown
    };

    QString toString(const beam::wallet::WalletID&);
    QString toString(const beam::Merkle::Hash&);
    QString AmountToString(const beam::Amount& value, Currencies coinType);
    QString toString(const beam::Timestamp& ts);
    double  Beam2Coins(const beam::Amount& value);

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
    };
}