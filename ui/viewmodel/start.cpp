// Copyright 2018 The Beam Team
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

#include "start.h"
#include "wallet\keystore.h"
#include <QMessageBox>

using namespace beam;
using namespace ECC;
using namespace std;

StartViewModel::StartViewModel(const string& walletStorage, const string& bbsStorage, StartViewModel::DoneCallback done)
	: _walletStorage(walletStorage)
    , _bbsStorage(bbsStorage)
	, _done(done)
{

}

bool StartViewModel::walletExists() const
{
	return Keychain::isInitialized(_walletStorage);
}

#ifdef WIN32
struct WSAInit {
	WSAInit() {
		WSADATA wsaData = {};
		int errorno = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (errorno != 0) {
			throw std::runtime_error("Failed to init WSA");
		}
	}
	~WSAInit() {
		WSACleanup();
	}
};
#endif

bool StartViewModel::createWallet(const QString& seed, const QString& pass, const QString& nodeAddrString)
{
#ifdef WIN32
	WSAInit init;
#endif // !WIN32

	io::Address nodeAddr;

	if (!nodeAddr.resolve(nodeAddrString.toStdString().c_str()))
	{
		return false;
	}

	NoLeak<uintBig> walletSeed;
	walletSeed.V = Zero;
	{
		Hash::Value hv;
		Hash::Processor() << seed.toStdString().c_str() >> hv;
		walletSeed.V = hv;
	}

    string walletPass = pass.toStdString();
	auto db = Keychain::init(_walletStorage, walletPass, walletSeed);

	if (db)
	{
        try
        {
            IKeyStore::Options options;
            options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
            options.fileName = _bbsStorage;

            IKeyStore::Ptr keystore = IKeyStore::create(options, walletPass.c_str(), walletPass.size());

            // generate default address
            WalletAddress defaultAddress = {};
            defaultAddress.m_own = true;
            defaultAddress.m_label = "default";
            defaultAddress.m_createTime = getTimestamp();
            defaultAddress.m_duration = numeric_limits<uint64_t>::max();
            keystore->gen_keypair(defaultAddress.m_walletID, walletPass.c_str(), walletPass.size(), true);

            db->saveAddress(defaultAddress);
        }
        catch (const std::runtime_error& ex)
        {
            QMessageBox::critical(0, "Error", "Failed to generate default address", QMessageBox::Ok);
        }
        db->setNodeAddr(nodeAddr);
		_done(db, pass.toStdString());
	}
	else return false;
}

bool StartViewModel::openWallet(const QString& pass)
{
	auto db = Keychain::open(_walletStorage, pass.toStdString());

	if (db)
	{
		_done(db, pass.toStdString());
	}
	else return false;
}
