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
        IWalletDB& storage,
        bool isEnabled)
        : m_isEnabled(isEnabled),
          m_broadcastGateway(broadcastGateway),
          m_validator(validator),
          m_storage(storage)
    {
        if (m_isEnabled) loadRatesToCache();
        m_broadcastGateway.registerListener(BroadcastContentType::ExchangeRates, this); // can register only once because of Protocol class implementation
    }

    void ExchangeRateProvider::setOnOff(bool isEnabled)
    {
        if (m_isEnabled != isEnabled)
        {
            if (isEnabled)
            {
                loadRatesToCache();
            }
            else
            {
                m_cache.clear();
            }

            m_isEnabled = isEnabled;
        }
    }

    void ExchangeRateProvider::loadRatesToCache()
    {
        const auto& rates = m_storage.getLatestExchangeRates();
        for (const auto& rate : rates)
        {
            const auto uniqID = std::make_pair(rate.from, rate.to);
            m_cache[uniqID] = rate;
        }
    }

    std::vector<ExchangeRate> ExchangeRateProvider::getRates()
    {
        std::vector<ExchangeRate> rates; 
        for (const auto& r : m_cache)
        {
            rates.push_back(r.second);
        }
        return rates;
    }

    void ExchangeRateProvider::processRates(const std::vector<ExchangeRate>& rates)
    {
        std::vector<ExchangeRate> changedRates;
        for (const auto& rate : rates)
        {
            const auto uniqID = std::make_pair(rate.from, rate.to);
            const auto storedRateIt = m_cache.find(uniqID);
            if (storedRateIt == std::cend(m_cache)
            || storedRateIt->second.updateTime < rate.updateTime)
            {
                m_cache[uniqID] = rate;
                m_storage.saveExchangeRate(rate);
                changedRates.emplace_back(rate);
            }
        }
        if (!changedRates.empty())
        {
            notifySubscribers(changedRates);
        }
    }

    bool ExchangeRateProvider::onMessage(uint64_t unused, ByteBuffer&& input)
    {
        if (m_isEnabled)
        {
            try
            {
                BroadcastMsg res;
                if (m_validator.processMessage(input, res))
                {
                    std::vector<ExchangeRate> receivedRates;
                    if (fromByteBuffer(res.m_content, receivedRates))
                    {
                        processRates(receivedRates);
                    }
                }
            }
            catch(...)
            {
                LOG_WARNING() << "broadcast message processing exception";
                return false;
            }
        }
        return true;
    }

    bool ExchangeRateProvider::onMessage(uint64_t unused, BroadcastMsg&& msg)
    {
        if (m_isEnabled && m_validator.isSignatureValid(msg))
        {
            try
            {
                std::vector<ExchangeRate> rates;
                if (fromByteBuffer(msg.m_content, rates))
                {
                    processRates(rates);
                }
            }
            catch(...)
            {
                LOG_WARNING() << "broadcast message processing exception";
                return false;
            }
        }
        return true;
    }

    void ExchangeRateProvider::Subscribe(IExchangeRatesObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(),
                            m_subscribers.end(),
                            observer);
        assert(it == m_subscribers.end());
        if (it == m_subscribers.end()) m_subscribers.push_back(observer);
    }

    void ExchangeRateProvider::Unsubscribe(IExchangeRatesObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(),
                            m_subscribers.end(),
                            observer);
        assert(it != m_subscribers.end());
        m_subscribers.erase(it);
    }

    void ExchangeRateProvider::notifySubscribers(const ExchangeRates& rates) const
    {
        for (const auto sub : m_subscribers)
        {
            sub->onExchangeRates(rates);
        }
    }
} // namespace beam::wallet
