#pragma once
#include <QObject>
#include "wallet/common.h"

namespace beamui
{
    QString toString(const beam::WalletID&);
    QString BeamToString(const beam::Amount& value);
    QString toString(const beam::Timestamp& ts);

    class Filter
    {
    public:
        Filter(size_t size = 12);
        void addSample(double value);
        double getAverage() const;
    private:
        std::vector<double> _samples;
        size_t _index;
    };
}