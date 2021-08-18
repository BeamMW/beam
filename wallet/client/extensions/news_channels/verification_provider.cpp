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
#include "verification_provider.h"
#include "wallet/core/common.h"
#include "utility/logger.h"

namespace beam::wallet
{
    VerificationProvider::VerificationProvider(IBroadcastMsgGateway& broadcastGateway, BroadcastMsgValidator& validator,  IWalletDB& wdb)
        : m_broadcastGateway(broadcastGateway)
        , m_validator(validator)
        , m_storage(wdb)
    {
        loadCache();
        m_broadcastGateway.registerListener(BroadcastContentType::AssetVerification, this);
    }

    bool VerificationProvider::onMessage(uint64_t unused, BroadcastMsg&& msg)
    {
        if (m_validator.isSignatureValid(msg))
        {
            try
            {
                std::vector<VerificationInfo> info;
                if (fromByteBuffer(msg.m_content, info))
                {
                    processInfo(info);
                    return true;
                }
            }
            catch(...)
            {
                LOG_WARNING() << "VerificationProvider: broadcast message processing exception";
            }
        }
        return false;
    }

    void VerificationProvider::Subscribe(IVerificationObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);
        assert(it == m_subscribers.end());
        if (it == m_subscribers.end()) m_subscribers.push_back(observer);
    }

    void VerificationProvider::Unsubscribe(IVerificationObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);
        assert(it != m_subscribers.end());
        m_subscribers.erase(it);
    }

    void VerificationProvider::notifySubscribers(const std::vector<VerificationInfo>& info) const
    {
        for (const auto sub : m_subscribers)
        {
            sub->onVerificationInfo(info);
        }
    }

    std::vector<VerificationInfo> VerificationProvider::getInfo() const
    {
        std::vector<VerificationInfo> result;
        for (const auto& pair: m_cache)
        {
            result.push_back(pair.second);
        }
        return result;
    }

    void VerificationProvider::loadCache()
    {
        const auto& info = m_storage.getVerification();
        for (const auto& vi: info)
        {
            m_cache[vi.m_assetID] = vi;
        }
    }

    void VerificationProvider::processInfo(const std::vector<VerificationInfo>& info)
    {
        for (const auto& vi: info)
        {
            const auto cached = m_cache.find(vi.m_assetID);
            if (cached == m_cache.end() || cached->second.m_updateTime < vi.m_updateTime)
            {
                m_storage.saveVerification(vi);
                m_cache[vi.m_assetID] = vi;
                m_changed.insert(vi.m_assetID);

                if (!m_updateTimer)
                {
                    m_updateTimer = io::Timer::create(io::Reactor::get_Current());
                    m_updateTimer->start(60, false, [this]() {
                        onUpdateTimer();
                    });
                }
            }
        }
    }

    void VerificationProvider::onUpdateTimer()
    {
        m_updateTimer.reset();
        std::vector<VerificationInfo> changed;
        changed.reserve(m_changed.size());

        for (const auto& aid : m_changed)
        {
            changed.push_back(m_cache[aid]);
        }

        m_changed.clear();
        notifySubscribers(changed);
    }
}
