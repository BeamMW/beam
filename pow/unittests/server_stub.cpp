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

#include "pow/external_pow.h"
#include "utility/io/reactor.h"
#include "utility/io/timer.h"
#include "utility/logger.h"

using namespace beam;

std::unique_ptr<IExternalPOW> server;
int id = 0;
Merkle::Hash hash;
Block::PoW pow;
static const unsigned TIMER_MSEC = 280000;
io::Timer::Ptr feedJobsTimer;

void got_new_block();

void gen_new_job() {
    ECC::GenRandom(&pow.m_Nonce, Block::PoW::NonceType::nBytes);
    ECC::GenRandom(pow.m_Indices.data(), Block::PoW::nSolutionBytes);
    //ECC::GenRandom(&pow.m_Difficulty.m_Packed, 4);
    pow.m_Difficulty = 0; //Difficulty(1 << Difficulty::s_MantissaBits);
    ECC::GenRandom(&hash.m_pData, 32);

    if (server) server->new_job(
        std::to_string(++id),
        hash, pow,
        &got_new_block,
        []() { return false; }
    );

    feedJobsTimer->start(TIMER_MSEC, false, &gen_new_job);
}

void got_new_block() {
    feedJobsTimer->cancel();
    if (server) {
        std::string id;
        server->get_last_found_block(id, pow);
        if (pow.IsValid(hash.m_pData, 32)) {
            LOG_INFO() << "got valid block" << TRACE(id);
        }
        gen_new_job();
    }
}

int main() {
    ECC::InitializeContext();

    auto logger = Logger::create(LOG_LEVEL_INFO, LOG_LEVEL_VERBOSE);
    int retCode = 0;
    try {
        io::Address listenTo = io::Address::localhost().port(20000);
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);
        feedJobsTimer = io::Timer::create(*reactor);
        IExternalPOW::Options options;
        options.certFile = PROJECT_SOURCE_DIR "/utility/unittest/test.crt";
        options.privKeyFile = PROJECT_SOURCE_DIR "/utility/unittest/test.key";
        server = IExternalPOW::create(options, *reactor, listenTo);
        gen_new_job();
        reactor->run();
        feedJobsTimer.reset();
        server.reset();
        LOG_INFO() << "Done";
    } catch (const std::exception& e) {
        LOG_ERROR() << "EXCEPTION: " << e.what();
        retCode = 255;
    } catch (...) {
        LOG_ERROR() << "NON_STD EXCEPTION";
        retCode = 255;
    }
    return retCode;

}

