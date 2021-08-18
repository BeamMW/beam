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
            const auto uniqID = std::make_pair(rate.m_from, rate.m_to);
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
        for (const auto& rate : rates)
        {
            const auto uniqID = std::make_pair(rate.m_from, rate.m_to);
            const auto storedRateIt = m_cache.find(uniqID);
            if (storedRateIt == std::cend(m_cache)
            || storedRateIt->second.m_updateTime < rate.m_updateTime)
            {
                m_cache[uniqID] = rate;
                m_storage.saveExchangeRate(rate);
                m_changedPairs.emplace(uniqID);
                if (!m_updateTimer)
                {
                    m_updateTimer = io::Timer::create(io::Reactor::get_Current());
                    m_updateTimer->start(60, false, [this]() { onUpdateTimer(); });
                }
            }
        }
    }

    bool ExchangeRateProvider::processRatesMessage(const ByteBuffer& buffer)
    {
        try
        {
            Block::SystemState::ID state;
            if (m_storage.getSystemStateID(state))
            {
                if (state.m_Height >= Rules::get().pForks[3].m_Height)
                {
                    std::vector<ExchangeRate> receivedRates;
                    if (fromByteBuffer(buffer, receivedRates))
                    {
                        processRates(receivedRates);
                    }
                }
                else
                {
                    std::vector<ExchangeRateF2> f2Rates;
                    if (fromByteBuffer(buffer, f2Rates))
                    {
                        std::vector<ExchangeRate> receivedRates;
                        for(const auto& r2: f2Rates)
                        {
                            auto newRate = ExchangeRate::FromERH2(r2);
                            receivedRates.push_back(newRate);
                        }

                        assert(receivedRates.size() == f2Rates.size());
                        processRates(receivedRates);
                    }
                }
            }
            // we simply ignore messages is wallet doesn't have synced
            return true;
        }
        catch(const std::exception& ex)
        {
            LOG_WARNING() << "broadcast rate message processing exception: " << ex.what();
            return false;
        }
        catch(...)
        {
            LOG_WARNING() << "broadcast rate message processing exception";
            return false;
        }
    }

    bool ExchangeRateProvider::onMessage(uint64_t unused, ByteBuffer&& input)
    {
        if (!m_isEnabled)
        {
            return true;
        }

        BroadcastMsg res;
        if (m_validator.processMessage(input, res))
        {
            return processRatesMessage(res.m_content);
        }

        return false;
    }

    bool ExchangeRateProvider::onMessage(uint64_t unused, BroadcastMsg&& msg)
    {
        if (!m_isEnabled)
        {
            return true;
        }
        
        if (m_validator.isSignatureValid(msg))
        {
            return processRatesMessage(msg.m_content);
        }

        return false;
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

    void ExchangeRateProvider::onUpdateTimer()
    {
        std::vector<ExchangeRate> changedRates;
        m_updateTimer.reset();
        changedRates.reserve(m_changedPairs.size());
        for (const auto& p : m_changedPairs)
        {
            changedRates.push_back(m_cache[p]);
        }
        m_changedPairs.clear();
        notifySubscribers(changedRates);
          
    }
} // namespace beam::wallet
