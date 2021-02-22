// Copyright 2021 The Beam Team
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

// #include "wallet/client/wallet_client.h"
#include <emscripten/bind.h>

using namespace emscripten;

class WasmClient : public WalletClient
{

}

struct WasmClientWrapper
{
public:
    WasmClientWrapper(const std::string& phrase)
    {
        _client = make_unique<WalletModel>(walletDB, "127.0.0.1:10005", reactor);
    }

private:
    std::unique_ptr<WasmClient> _client;

};
// Binding code
EMSCRIPTEN_BINDINGS() 
{
    class_<WasmClientWrapper>("WasmClientWrapper")
        .constructor<const std::string&>()
        // .function("getOwnerKey",            &KeyKeeper::GetOwnerKey)
        // .function("getWalletID",            &KeyKeeper::GetWalletID)
        // .function("getIdentity",            &KeyKeeper::GetIdentity)
        // .function("getSendToken",           &KeyKeeper::GetSendToken)
        // .function("getSbbsAddress",         &KeyKeeper::GetSbbsAddress)
        // .function("getSbbsAddressPrivate",  &KeyKeeper::GetSbbsAddressPrivate)
        // .function("invokeServiceMethod",    &KeyKeeper::InvokeServiceMethod)
        // .class_function("GeneratePhrase",   &KeyKeeper::GeneratePhrase)
        // .class_function("IsAllowedWord",    &KeyKeeper::IsAllowedWord)
        // .class_function("IsValidPhrase",    &KeyKeeper::IsValidPhrase)
        // .class_function("ConvertTokenToJson",&KeyKeeper::ConvertTokenToJson)
        // .class_function("ConvertJsonToToken", &KeyKeeper::ConvertJsonToToken)
        ;
}