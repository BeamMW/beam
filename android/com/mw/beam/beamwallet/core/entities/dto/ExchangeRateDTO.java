// Copyright 2020 The Beam Team
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

package com.mw.beam.beamwallet.core.entities.dto;

public class ExchangeRateDTO
{
    public int currency;
    public int unit;            // unit of m_rate measurment, e.g. USD or any other currency
    public long amount;         // value as decimal fixed point. m_rate = 100,000,000 is 1 unit
    public long updateTime;

    public enum Currency
    {
        Beam,
        Bitcoin,
        Litecoin,
        Denarius,
        Qtum,
        Usd,
        Unknown
    }
}
