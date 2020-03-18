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

#include "exchange_rate_provider.h"

#include "utility/logger.h"

namespace beam::wallet
{
    ExchangeRateProvider::ExchangeRateProvider(
        IBroadcastMsgGateway& broadcastGateway,
        BroadcastMsgValidator& validator,
        IWalletDB& storage)
        : m_broadcastGateway(broadcastGateway),
          m_validator(validator),
          m_storage(storage)
    {
        loadRatesToCache();
        m_broadcastGateway.registerListener(BroadcastContentType::ExchangeRates, this);
    }

    void ExchangeRateProvider::loadRatesToCache()
    {
        const auto& rates = m_storage.getExchangeRates();
        for (const auto& rate : rates)
        {
            const auto uniqID = std::make_pair(rate.m_currency, rate.m_unit);
            m_cache[uniqID] = rate;
        }
    }

    std::vector<ExchangeRate> ExchangeRateProvider::getRates()
    {
        std::vector<ExchangeRate> rates; 
        for (const auto r : m_cache)
        {
            rates.push_back(r.second);
        }
        return rates;
    }

    bool ExchangeRateProvider::onMessage(uint64_t unused, ByteBuffer&& input)
    {
        try
        {
            BroadcastMsg res;
            if (m_validator.processMessage(input, res))
            {
                std::vector<ExchangeRate> receivedRates;
                if (fromByteBuffer(res.m_content, receivedRates))
                {
                    std::vector<ExchangeRate> changedRates;
                    for (const auto& receivedRate : receivedRates)
                    {
                        const auto uniqID = std::make_pair(receivedRate.m_currency, receivedRate.m_unit);
                        const auto storedRateIt = m_cache.find(uniqID);
                        if (storedRateIt == std::cend(m_cache)
                         || storedRateIt->second.m_updateTime < receivedRate.m_updateTime)
                        {
                            m_cache[uniqID] = receivedRate;
                            m_storage.saveExchangeRate(receivedRate);
                            changedRates.emplace_back(receivedRate);
                        }
                    }
                    if (!changedRates.empty())
                    {
                        notifySubscribers(changedRates);
                    }
                }
            }
        }
        catch(...)
        {
            LOG_WARNING() << "broadcast message processing exception";
            return false;
        }
        return true;
    }

    void ExchangeRateProvider::Subscribe(IExchangeRateObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(),
                            m_subscribers.end(),
                            observer);
        assert(it == m_subscribers.end());
        if (it == m_subscribers.end()) m_subscribers.push_back(observer);
    }

    void ExchangeRateProvider::Unsubscribe(IExchangeRateObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(),
                            m_subscribers.end(),
                            observer);
        assert(it != m_subscribers.end());
        m_subscribers.erase(it);
    }

    void ExchangeRateProvider::notifySubscribers(const std::vector<ExchangeRate>& rates) const
    {
        for (const auto sub : m_subscribers)
        {
            sub->onExchangeRates(rates);
        }
    }
} // namespace beam::wallet
