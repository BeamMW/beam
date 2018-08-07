#pragma once
#include <QObject>
#include "wallet/common.h"

namespace beamui
{
    QString toString(const beam::WalletID&);
    QString BeamToString(const beam::Amount& value);
    void ltrim(std::string &s, char sym);
    QString toString(const beam::Timestamp& ts);
}