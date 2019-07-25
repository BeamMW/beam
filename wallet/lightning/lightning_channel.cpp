// Copyright 2019 The Beam Team
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

#include "lightning_channel.h"

namespace beam::wallet::lightning
{
LightningChannel::~LightningChannel()
{

}

Height LightningChannel::get_Tip() const
{
    Block::SystemState::Full tip;
    m_WalletDB->get_History().get_Tip(tip);
    return tip.m_Height;
}

proto::FlyClient::INetwork& LightningChannel::get_Net()
{
    return *(m_net.get());
}

Amount LightningChannel::SelectInputs(
        std::vector<Key::IDV>& vInp, Amount valRequired)
{
    assert(vInp.empty());

    Amount nDone = 0;
    auto coins = m_WalletDB->selectCoins(valRequired);
    vInp.reserve(coins.size());
    std::transform(coins.begin(), coins.end(), std::back_inserter(vInp),
                   [&nDone] (const Coin& coin) -> Key::IDV
                    {
                        auto idv = coin.m_ID;
                        nDone += idv.m_Value;
                        return idv;
                    });
    return nDone;
}

void LightningChannel::get_Kdf(Key::IKdf::Ptr& pKdf)
{
    pKdf = m_WalletDB->get_MasterKdf();
}

void LightningChannel::AllocTxoID(Key::IDV& kidv)
{
    kidv.set_Subkey(0);
    kidv.m_Idx = get_RandomID();
}

void LightningChannel::SendPeer(Negotiator::Storage::Map&& dataOut)
{
    assert(!dataOut.empty());

    WalletAddress address;
    if (m_SendMyWid)
    {
        m_SendMyWid = false;
        std::cout << "Generate new WID\n";
        WalletAddress::ExpirationStatus expirationStatus = WalletAddress::ExpirationStatus::Never;
        
        address = storage::createAddress(*m_WalletDB);

        address.setExpiration(expirationStatus);
        address.m_label = "laser";
        // m_WalletDB->saveAddress(address);
        dataOut.Set(address.m_walletID, Codes::MyWid);
    }
    
    BbsChannel ch;
    address.m_walletID.m_Channel.Export(ch);
    get_Net().BbsSubscribe(ch, getTimestamp(), &m_BbsReceiver);

    Serializer ser;
    ser & m_ID;
    ser & Cast::Down<FieldMap>(dataOut);

    std::cout << "SendPeer\tTo peer (via bbs): " << ser.buffer().second << std::endl;

    proto::FlyClient::RequestBbsMsg::Ptr pReq(new proto::FlyClient::RequestBbsMsg);
	m_widTrg.m_Channel.Export(pReq->m_Msg.m_Channel);

	ECC::NoLeak<ECC::Hash::Value> hvRandom;
	ECC::GenRandom(hvRandom.V);

	ECC::Scalar::Native nonce;
    Key::IKdf::Ptr pKdf;
    get_Kdf(pKdf);
	pKdf->DeriveKey(nonce, hvRandom.V);

	if (proto::Bbs::Encrypt(pReq->m_Msg.m_Message, m_widTrg.m_Pk, nonce, ser.buffer().first, static_cast<uint32_t>(ser.buffer().second)))
	{
		// skip mining!
		pReq->m_Msg.m_TimePosted = getTimestamp();
		get_Net().PostRequest(*pReq, m_openHandler);
	}
};

}  // namespace beam::wallet::lightning
