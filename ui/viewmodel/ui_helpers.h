#pragma once
#include <QObject>
#include "wallet/core/common.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/transactions/swaps/common.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

Q_DECLARE_METATYPE(beam::wallet::TxID)
Q_DECLARE_METATYPE(beam::wallet::TxParameters)
Q_DECLARE_METATYPE(ECC::uintBig)

namespace beamui
{
    enum class Currencies
    {
        Beam,
        Bitcoin,
        Litecoin,
        Qtum,
        Usd,
        Unknown
    };

    QString toString(Currencies currency);
    std::string toStdString(Currencies currency);
    QString toString(const beam::wallet::WalletID&);
    QString toString(const beam::Merkle::Hash&);
    // convert amount to ui string with "." as a separator
    QString AmountToUIString(const beam::Amount& value, Currencies coinType = Currencies::Unknown);
    QString AmountInGrothToUIString(const beam::Amount& value);
    // expects ui string with a "." as a separator
    beam::Amount UIStringToAmount(const QString& value);
    QString toString(const beam::Timestamp& ts);
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    Currencies convertSwapCoinToCurrency(beam::wallet::AtomicSwapCoin coin);
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

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
    QString convertBeamHeightDiffToTime(int32_t dt);

    beam::Version getCurrentAppVersion();

}  // namespace beamui
