#pragma once
#include <QObject>
#include "wallet/common.h"

Q_DECLARE_METATYPE(beam::wallet::TxID)
Q_DECLARE_METATYPE(beam::wallet::TxParameters)

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

    QString toString(Currencies currency);
    std::string toStdString(Currencies currency);
    QString toString(const beam::wallet::WalletID&);
    QString toString(const beam::Merkle::Hash&);
    // convert amount to ui string with "." as a separator
    QString AmountToUIString(const beam::Amount& value, Currencies coinType = Currencies::Unknown);
    // expects ui string with a "." as a separator
    beam::Amount UIStringToAmount(const QString& value);
    QString toString(const beam::Timestamp& ts);
    Currencies convertSwapCoinToCurrency(beam::wallet::AtomicSwapCoin coin);

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
        bool _is_poor;
    };
    QDateTime CalculateExpiresTime(beam::Height currentHeight, beam::Height expiresHeight);
    QString getEstimateTimeStr(int estimate);
}  // namespace beamui
