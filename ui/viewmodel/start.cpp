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

using namespace beam;
using namespace ECC;

StartViewModel::StartViewModel(const std::string& walletStorage, StartViewModel::Done done)
	: _walletStorage(walletStorage)
	, _done(done)
{

}

bool StartViewModel::walletExists() const
{
	return Keychain::isInitialized(_walletStorage);
}

bool StartViewModel::createWallet(const QString& seed, const QString& pass)
{
	NoLeak<uintBig> walletSeed;
	walletSeed.V = Zero;
	{
		Hash::Value hv;
		Hash::Processor() << seed.toStdString().c_str() >> hv;
		walletSeed.V = hv;
	}

	auto db = Keychain::init(_walletStorage, pass.toStdString(), walletSeed);

	if (db)
	{
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
