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


// test helpers and mocks
#include "test_helpers.h"
WALLET_TEST_INIT
#include "mock_bbs_network.cpp"

// tested module
#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/client/extensions/offers_board/swap_offers_board.h"
#include "wallet/transactions/swaps/swap_transaction.h"
#include "wallet/transactions/swaps/utils.h"

// dependencies
#include "keykeeper/local_private_key_keeper.h"
#include "utility/hex.h"

#include <boost/filesystem.hpp>

using namespace beam;
using namespace beam::wallet;
using namespace std;

namespace
{
    using PrivateKey = ECC::Scalar::Native;
    using PublicKey = PeerID;

    const string dbFileName = "wallet.db";
    constexpr Height Fork1Height = Height(10);
    constexpr Height Fork2Height = Height(20);

    void PublishOfferNoThrow(const SwapOffersBoard& board, const SwapOffer& offer)
    {
        try {
            board.publishOffer(offer);
        }
        catch(const SwapOffersBoard::InvalidOfferException & e) {
            std::cout << offer.m_txId << e.what() << endl;
        }
        catch(const SwapOffersBoard::OfferAlreadyPublishedException & e) {
            std::cout << offer.m_txId << e.what() << endl;
        }
        catch(const SwapOffersBoard::ForeignOfferException & e) {
            std::cout << offer.m_txId << e.what() << endl;
        }
        catch(const SwapOffersBoard::ExpiredOfferException & e) {
            std::cout << offer.m_txId << e.what() << endl;
        }
        catch(const SwapOffersBoard::OfferLifetimeExceeded & e) {
            std::cout << offer.m_txId << e.what() << endl;
        }
    }

    /**
     *  Class to test correct notification of SwapOffersBoard observers
     */
    struct MockBoardObserver : public ISwapOffersObserver
    {
        using CheckerFunction = function<void(ChangeAction, const vector<SwapOffer>&)>;

        MockBoardObserver(CheckerFunction checker) :
            m_testChecker(checker) {};

        void onSwapOffersChanged(ChangeAction action, const vector<SwapOffer>& offers) override
        {
            m_testChecker(action, offers);
        }

        CheckerFunction m_testChecker;
    };

    struct MockBroadcastListener : public IBroadcastListener
    {
        using OnMessage = function<void(BroadcastMsg&)>;

        MockBroadcastListener(OnMessage func) : m_callback(func) {};

        bool onMessage(BroadcastMsg&& msg) override
        {
            m_callback(msg);
            return true;
        };

        OnMessage m_callback;
    };

    IWalletDB::Ptr createSqliteWalletDB()
    {
        if (boost::filesystem::exists(dbFileName))
        {
            boost::filesystem::remove(dbFileName);
        }
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = 10283UL;
        auto walletDB = WalletDB::init(dbFileName, string("pass123"), seed);
        HeightHash id = { };
        id.m_Height = 134;
        walletDB->setSystemStateID(id);
        return walletDB;
    }

    // Generate random TxID
    TxID generateTxID()
    {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        TxID txId;
        for (uint8_t& i : txId)
        {
            i = std::rand() % 255;
        }
        return txId;
    }

    // Increment by 1 @id
    TxID& operator++(TxID& id)
    {
        for (uint8_t& i : id)
        {
            if (i < 0xff)
            {
                ++i;
                break;
            }
        }
        return id;
    }

    // Construct SwapOffer with random tx parameters.
    SwapOffer createOffer(const TxID& i, SwapOfferStatus s, const WalletID& k, AtomicSwapCoin c, bool o)
    {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        SwapOffer offer(i, s, k, c, o);
        // mandatory parameters
        offer.SetParameter(TxParameterID::AtomicSwapCoin, offer.m_coin);
        offer.SetParameter(TxParameterID::AtomicSwapIsBeamSide, std::rand() % 2);
        offer.SetParameter(TxParameterID::Amount, Amount(std::rand() % 10000));
        offer.SetParameter(TxParameterID::AtomicSwapAmount, Amount(std::rand() % 1000));
        offer.SetParameter(TxParameterID::MinHeight, Height(Fork1Height));
        offer.SetParameter(TxParameterID::PeerResponseTime, Height(10));
        offer.SetParameter(TxParameterID::TransactionType, TxType::AtomicSwap);
        return offer;
    }

    /**
     *  Generate random offer.
     *  Create address in database. Use random TxID.
     *  return generated swap offer and address key derivation index
     */
    std::tuple<SwapOffer, uint64_t> generateTestOffer(IWalletDB::Ptr walletDB)
    {
        WalletAddress wa;
        walletDB->createAddress(wa);
        walletDB->saveAddress(wa);
        TxID txID = generateTxID();
        const auto offer = createOffer( txID,
                                        SwapOfferStatus::Pending,
                                        wa.m_BbsAddr,
                                        AtomicSwapCoin::Bitcoin,
                                        true);
        return std::make_tuple(offer, wa.m_OwnID);
    }

    /**
     *  Derive key pair with specified @keyIndex
     */
    std::tuple<PublicKey, PrivateKey> deriveKeypair(IWalletDB::Ptr walletDB, uint64_t keyIndex)
    {
        PrivateKey sk;
        PublicKey pk;
        walletDB->get_MasterKdf()->DeriveKey(sk, ECC::Key::ID(keyIndex, Key::Type::Bbs));
        pk.FromSk(sk);
        return std::make_tuple(pk, sk);
    }
    
    /**
     *  Create signature for @data using key derived with specified @keyIndex
     *  return signature
     */
    ByteBuffer signData(const ByteBuffer& data, uint64_t keyIndex, IWalletDB::Ptr walletDB)
    {
        PrivateKey sk;
        std::tie(std::ignore, sk) = deriveKeypair(walletDB, keyIndex);
        SignatureHandler signHandler;
        signHandler.m_data = data;
        signHandler.Sign(sk);
        ByteBuffer rawSignature = toByteBuffer(signHandler.m_Signature);
        return rawSignature;
    }

    /**
     *  Create message according to protocol.
     *  Concatenate message body and signature.
     */
    BroadcastMsg makeMsg(const ByteBuffer& msgRaw, const ByteBuffer& signatureRaw)
    {
        assert(msgRaw.size() + signatureRaw.size());
        BroadcastMsg fullMsg{ msgRaw, signatureRaw };
        return fullMsg;
    }    

    void TestProtocolHandlerSignature()
    {
        cout << endl << "Test protocol handler validating signature" << endl;

        auto storage = createSqliteWalletDB();

        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());

        {
            std::cout << "Case: parsing message with invalid signature" << endl;

            const auto& [offer, keyIndex] = generateTestOffer(storage);

            const ByteBuffer msgRaw = toByteBuffer(SwapOfferToken(offer));
            auto signatureRaw = signData(msgRaw, keyIndex, storage);
            // corrupt signature
            signatureRaw.front() += 1;

            const auto finalMessage = makeMsg(msgRaw, signatureRaw);
            
            boost::optional<SwapOffer> res;
            WALLET_CHECK_NO_THROW(res = protocolHandler.parseMessage(finalMessage));
            WALLET_CHECK(!res);
        }
        {
            std::cout << "Case: parsing message with invalid public key" << endl;

            auto [offer, keyIndex] = generateTestOffer(storage);

            // changed public key another
            WalletAddress anotherAddress;
            storage->createAddress(anotherAddress);
            offer.m_publisherId = anotherAddress.m_BbsAddr;

            const ByteBuffer msgRaw = toByteBuffer(SwapOfferToken(offer));
            const auto signatureRaw = signData(msgRaw, keyIndex, storage);
            const auto finalMessage = makeMsg(msgRaw, signatureRaw);

            boost::optional<SwapOffer> res;
            WALLET_CHECK_NO_THROW(res = protocolHandler.parseMessage(finalMessage));
            WALLET_CHECK(!res);
        }
        {
            std::cout << "Case: parsing correct message" << endl;

            const auto& [offer, keyIndex] = generateTestOffer(storage);

            const ByteBuffer msgRaw = toByteBuffer(SwapOfferToken(offer));
            const auto signatureRaw = signData(msgRaw, keyIndex, storage);
            const auto finalMessage = makeMsg(msgRaw, signatureRaw);

            boost::optional<SwapOffer> res;
            WALLET_CHECK_NO_THROW(res = protocolHandler.parseMessage(finalMessage));
            WALLET_CHECK(res);
            WALLET_CHECK(*res == offer);
        }

        cout << "Test end" << endl;
    }

    void TestProtocolHandlerIntegration()
    {
        cout << endl << "Test protocol handler integration" << endl;

        auto storage = createSqliteWalletDB();
        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouter(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());

        {
            std::cout << "Case: create, dispatch and parse offer" << endl;

            SwapOffer offer;
            std::tie(offer, std::ignore) = generateTestOffer(storage);
            bool executed = false;

            MockBroadcastListener testListener(
                [&executed, &offer, &protocolHandler]
                (BroadcastMsg& msg)
                {
                    boost::optional<SwapOffer> res;
                    WALLET_CHECK_NO_THROW(res = protocolHandler.parseMessage(msg));
                    WALLET_CHECK(res);
                    WALLET_CHECK(*res == offer);
                    executed = true;
                });
            broadcastRouter.registerListener(BroadcastContentType::SwapOffers, &testListener);

            BroadcastMsg msg;
            WALLET_CHECK_NO_THROW(msg = protocolHandler.createBroadcastMessage(offer, storage->getAddress(offer.m_publisherId)->m_OwnID));

            broadcastRouter.sendMessage(BroadcastContentType::SwapOffers, msg);

            WALLET_CHECK(executed);
        }

        cout << "Test end" << endl;
    }

    void TestMandatoryParameters()
    {
        cout << endl << "Test mandatory parameters validation" << endl;

        auto storage = createSqliteWalletDB();

        SwapOffer correctOffer;
        std::tie(correctOffer, std::ignore) = generateTestOffer(storage);
        TxID txID = correctOffer.m_txId;    // used to iterate and create unique ID's

        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouter(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        SwapOffersBoard Alice(broadcastRouter, protocolHandler, storage);

        HeightHash startState;
        startState.m_Height = Fork1Height;
        Alice.onSystemStateChanged(startState);

        WALLET_CHECK(Alice.getOffersList().size() == 0);

        size_t offersCount = 0;
        size_t count = 0;
        {
            cout << "Case: mandatory parameters presence:" << endl;
            const std::array<TxParameterID,6> mandatoryParams {
                TxParameterID::AtomicSwapCoin,
                TxParameterID::AtomicSwapIsBeamSide,
                TxParameterID::Amount,
                TxParameterID::AtomicSwapAmount,
                TxParameterID::MinHeight,
                TxParameterID::PeerResponseTime };

            for (auto parameter : mandatoryParams)
            {
                // check that offers without mandatory parameters don't appear on board
                SwapOffer o = correctOffer;
                o.m_txId = ++txID;
                cout << "\tparameter code " << static_cast<uint32_t>(parameter) << endl;
                o.DeleteParameter(parameter);
                PublishOfferNoThrow(Alice, o);
                WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
                WALLET_CHECK(count == offersCount);
            }
        }
        {
            cout << "Case: AtomicSwapCoin parameter validation" << endl;
            SwapOffer o = correctOffer;
            o.m_txId = ++txID;
            o.m_coin = AtomicSwapCoin::Unknown;
            PublishOfferNoThrow(Alice, o);
            WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
            WALLET_CHECK(count == offersCount);
        }
        {
            cout << "Case: SwapOfferStatus parameter validation" << endl;
            SwapOffer o = correctOffer;
            o.m_txId = ++txID;
            o.m_status = static_cast<SwapOfferStatus>(static_cast<uint32_t>(SwapOfferStatus::Failed) + 1);
            PublishOfferNoThrow(Alice, o);
            WALLET_CHECK_NO_THROW(count = Alice.getOffersList().size());
            WALLET_CHECK(count == offersCount);
        }
        {
            cout << "Case: correct offer" << endl;
            SwapOffer o = correctOffer;
            o.m_txId = ++txID;
            PublishOfferNoThrow(Alice, o);
            WALLET_CHECK(Alice.getOffersList().size() == ++offersCount);
        }
        cout << "Test end" << endl;
    }

    void TestRawErc20WireCoinRejected()
    {
        cout << endl << "Test raw Erc20Token wire coin is rejected" << endl;

        // A legitimate publisher never emits AtomicSwapCoin::Erc20Token as the
        // top-level wire coin - it always substitutes ExtendedOffer (see
        // SwapOffersBoard::broadcastOffer / SwapOffer::IsExtended). So craft
        // the wire message directly (bypassing publishOffer's substitution)
        // to simulate a malformed/malicious peer sending raw Erc20Token.
        auto storage = createSqliteWalletDB();
        WalletAddress wa;
        storage->createAddress(wa);
        storage->saveAddress(wa);

        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouter(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        SwapOffersBoard Alice(broadcastRouter, protocolHandler, storage);

        HeightHash startState;
        startState.m_Height = Fork1Height;
        Alice.onSystemStateChanged(startState);

        WALLET_CHECK(Alice.getOffersList().size() == 0);

        SwapOffer offer = createOffer(generateTxID(), SwapOfferStatus::Pending, wa.m_BbsAddr, AtomicSwapCoin::Erc20Token, true);

        BroadcastMsg msg;
        WALLET_CHECK_NO_THROW(msg = protocolHandler.createBroadcastMessage(offer, wa.m_OwnID));

        broadcastRouter.sendMessage(BroadcastContentType::SwapOffers, msg);

        WALLET_CHECK(Alice.getOffersList().size() == 0);

        // Delivery control: the same board/router wiring must accept a
        // CLASSIC offer, proving the rejection above isn't a broken-delivery
        // artifact.
        SwapOffer classicOffer = createOffer(generateTxID(), SwapOfferStatus::Pending, wa.m_BbsAddr, AtomicSwapCoin::Bitcoin, true);

        BroadcastMsg classicMsg;
        WALLET_CHECK_NO_THROW(classicMsg = protocolHandler.createBroadcastMessage(classicOffer, wa.m_OwnID));

        broadcastRouter.sendMessage(BroadcastContentType::SwapOffers, classicMsg);

        WALLET_CHECK(Alice.getOffersList().size() == 1);

        cout << "Test end" << endl;
    }

    void TestExtendedErc20OfferDecimalsBounded()
    {
        cout << endl << "Test extended Erc20Token offer decimals bound (kMaxTokenDecimals)" << endl;

        // decimals is attacker-controlled: it's carried in a peer's offer-board
        // message (TxParameterID::AtomicSwapTokenDecimals), sourced from the
        // counterparty's ERC-20 contract. isExtendedOfferDataValid() must drop an
        // extended Erc20Token offer whose decimals exceeds kMaxTokenDecimals,
        // otherwise it feeds ethereum::TokenUnitsMultiplier's unbounded
        // 10^(decimals-9) math downstream.
        auto storage = createSqliteWalletDB();
        WalletAddress wa;
        storage->createAddress(wa);
        storage->saveAddress(wa);

        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouter(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        SwapOffersBoard Alice(broadcastRouter, protocolHandler, storage);

        HeightHash startState;
        startState.m_Height = Fork1Height;
        Alice.onSystemStateChanged(startState);

        WALLET_CHECK(Alice.getOffersList().size() == 0);

        auto makeErc20Offer = [&](uint8_t decimals)
        {
            // AtomicSwapCoin param carries the real coin (Erc20Token); the wire coin
            // is overridden to ExtendedOffer afterwards to mirror what
            // SwapOffersBoard::broadcastOffer does for a legitimate publisher (see
            // TestRawErc20WireCoinRejected above for why a raw wire Erc20Token is
            // instead rejected outright, before isExtendedOfferDataValid ever runs).
            SwapOffer offer = createOffer(generateTxID(), SwapOfferStatus::Pending, wa.m_BbsAddr, AtomicSwapCoin::Erc20Token, true);
            offer.SetParameter(TxParameterID::AtomicSwapTokenContract, std::string("0x0000000000000000000000000000000000000001"));
            offer.SetParameter(TxParameterID::AtomicSwapTokenSymbol, std::string("TKN"));
            offer.SetParameter(TxParameterID::AtomicSwapTokenDecimals, decimals);
            offer.m_coin = AtomicSwapCoin::ExtendedOffer;
            return offer;
        };

        {
            cout << "\tCase: decimals == kMaxTokenDecimals + 1 (19) is rejected" << endl;
            SwapOffer offer = makeErc20Offer(kMaxTokenDecimals + 1);

            BroadcastMsg msg;
            WALLET_CHECK_NO_THROW(msg = protocolHandler.createBroadcastMessage(offer, wa.m_OwnID));
            broadcastRouter.sendMessage(BroadcastContentType::SwapOffers, msg);

            WALLET_CHECK(Alice.getOffersList().size() == 0);
        }
        {
            cout << "\tCase: decimals == kMaxTokenDecimals (18) is accepted" << endl;
            SwapOffer offer = makeErc20Offer(kMaxTokenDecimals);

            BroadcastMsg msg;
            WALLET_CHECK_NO_THROW(msg = protocolHandler.createBroadcastMessage(offer, wa.m_OwnID));
            broadcastRouter.sendMessage(BroadcastContentType::SwapOffers, msg);

            WALLET_CHECK(Alice.getOffersList().size() == 1);

            // Params must survive the accept round trip intact.
            auto offersList = Alice.getOffersList();
            const SwapOffer& received = offersList.front();
            WALLET_CHECK(received.ResolveCoin() == AtomicSwapCoin::Erc20Token);
            auto contract = received.GetParameter<std::string>(TxParameterID::AtomicSwapTokenContract);
            WALLET_CHECK(contract && *contract == "0x0000000000000000000000000000000000000001");
            auto symbol = received.GetParameter<std::string>(TxParameterID::AtomicSwapTokenSymbol);
            WALLET_CHECK(symbol && *symbol == "TKN");
            auto receivedDecimals = received.GetParameter<uint8_t>(TxParameterID::AtomicSwapTokenDecimals);
            WALLET_CHECK(receivedDecimals && *receivedDecimals == kMaxTokenDecimals);
        }
        {
            cout << "\tCase: malformed contract address (not 0x + 40 hex) is rejected" << endl;
            SwapOffer offer = makeErc20Offer(kMaxTokenDecimals);
            offer.SetParameter(TxParameterID::AtomicSwapTokenContract, std::string("not-a-contract-address"));

            BroadcastMsg msg;
            WALLET_CHECK_NO_THROW(msg = protocolHandler.createBroadcastMessage(offer, wa.m_OwnID));
            broadcastRouter.sendMessage(BroadcastContentType::SwapOffers, msg);

            // still just the single valid offer accepted in the case above
            WALLET_CHECK(Alice.getOffersList().size() == 1);
        }
        {
            cout << "\tCase: oversized/garbage symbol is rejected" << endl;
            SwapOffer offer = makeErc20Offer(kMaxTokenDecimals);
            // 33 chars (over the 32-char bound) with a non-printable byte mixed in.
            std::string garbageSymbol(33, 'X');
            garbageSymbol[5] = '\x01';
            offer.SetParameter(TxParameterID::AtomicSwapTokenSymbol, garbageSymbol);

            BroadcastMsg msg;
            WALLET_CHECK_NO_THROW(msg = protocolHandler.createBroadcastMessage(offer, wa.m_OwnID));
            broadcastRouter.sendMessage(BroadcastContentType::SwapOffers, msg);

            WALLET_CHECK(Alice.getOffersList().size() == 1);
        }

        cout << "Test end" << endl;
    }

    void TestCommunication()
    {
        cout << endl << "Test boards communication and notification" << endl;

        auto storage = createSqliteWalletDB();
        
        SwapOffer correctOffer;
        std::tie(correctOffer, std::ignore) =  generateTestOffer(storage);
        TxID txID = correctOffer.m_txId;    // used to iterate and create unique ID's
        
        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouterA(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        BroadcastRouter broadcastRouterB(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        BroadcastRouter broadcastRouterC(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());

        SwapOffersBoard Alice(broadcastRouterA, protocolHandler, storage);
        SwapOffersBoard Bob(broadcastRouterB, protocolHandler, storage);
        SwapOffersBoard Cory(broadcastRouterC, protocolHandler, storage);

        WALLET_CHECK(Alice.getOffersList().size() == 0);
        WALLET_CHECK(Bob.getOffersList().size() == 0);
        WALLET_CHECK(Cory.getOffersList().size() == 0);
        
        HeightHash fork2state;
        HeightHash startState;
        fork2state.m_Height = Fork2Height;
        startState.m_Height = Fork1Height;
        // set offers not to expire during fork test
        correctOffer.SetParameter(TxParameterID::MinHeight, Fork1Height);
        correctOffer.SetParameter(TxParameterID::PeerResponseTime, (Fork2Height - Fork1Height) * Height(2));
        
        size_t offersCount = 0;
        {
            uint32_t executionCount = 0;
            MockBoardObserver testObserver(
                [&executionCount]
                (ChangeAction action, const vector<SwapOffer>& offers)
                {
                    WALLET_CHECK(action == ChangeAction::Added);
                    WALLET_CHECK(offers.size() == 1);
                    executionCount++;
                });
            Alice.Subscribe(&testObserver);
            Bob.Subscribe(&testObserver);
            Cory.Subscribe(&testObserver);
            
            Alice.onSystemStateChanged(startState);
            Bob.onSystemStateChanged(startState);
            Cory.onSystemStateChanged(startState);
            
            cout << "Case: normal dispatch and notification" << endl;
            SwapOffer o1 = correctOffer;
            SwapOffer o2 = correctOffer;
            SwapOffer o3 = correctOffer;
            o2.m_txId = ++txID;
            o3.m_txId = ++txID;
            PublishOfferNoThrow(Alice, o1);
            PublishOfferNoThrow(Bob, o2);
            PublishOfferNoThrow(Cory, o3);
            offersCount += 3;
            // everybody has to receive offer
            WALLET_CHECK(Alice.getOffersList().size() == offersCount);
            WALLET_CHECK(Bob.getOffersList().size() == offersCount);
            WALLET_CHECK(Cory.getOffersList().size() == offersCount);
            WALLET_CHECK(executionCount == offersCount * 3);
            {
                // check mandatory offer parameters
                auto receivedOffer = Bob.getOffersList().front();
                std::array<TxParameterID,6> paramsToCompare {
                    TxParameterID::AtomicSwapCoin,
                    TxParameterID::AtomicSwapIsBeamSide,
                    TxParameterID::Amount,
                    TxParameterID::AtomicSwapAmount,
                    TxParameterID::MinHeight,
                    TxParameterID::PeerResponseTime
                };
                for (auto p : paramsToCompare)
                {
                    auto receivedValue = receivedOffer.GetParameter<ByteBuffer>(p);
                    auto dispatchedValue = correctOffer.GetParameter<ByteBuffer>(p);
                    WALLET_CHECK(receivedValue && dispatchedValue);
                    WALLET_CHECK(*receivedValue == *dispatchedValue);
                }
            }

            cout << "Case: fork 2 happens" << endl;
            Alice.onSystemStateChanged(fork2state);
            Bob.onSystemStateChanged(fork2state);
            Cory.onSystemStateChanged(fork2state);
            
            cout << "Case: ignore same TxID" << endl;
            SwapOffer o4 = correctOffer;
            o4.m_coin = AtomicSwapCoin::Qtum;
            PublishOfferNoThrow(Cory, o4);
            WALLET_CHECK(Alice.getOffersList().size() == offersCount);
            WALLET_CHECK(Bob.getOffersList().size() == offersCount);
            WALLET_CHECK(Cory.getOffersList().size() == offersCount);
            WALLET_CHECK(Alice.getOffersList().front().m_coin == AtomicSwapCoin::Bitcoin);
            WALLET_CHECK(executionCount == offersCount * 3);

            cout << "Case: different TxID" << endl;
            o4.m_txId = ++txID;
            o4.m_coin = AtomicSwapCoin::Qtum;
            PublishOfferNoThrow(Cory, o4);
            offersCount++;
            WALLET_CHECK(Alice.getOffersList().size() == offersCount);
            WALLET_CHECK(Bob.getOffersList().size() == offersCount);
            WALLET_CHECK(Cory.getOffersList().size() == offersCount);
            WALLET_CHECK(executionCount == offersCount * 3);

            Alice.Unsubscribe(&testObserver);
            Bob.Unsubscribe(&testObserver);
            Cory.Unsubscribe(&testObserver);

            cout << "Case: unsubscribe stops notification" << endl;
            o4 = correctOffer;
            o4.m_txId = ++txID;
            o4.m_coin = AtomicSwapCoin::Litecoin;
            PublishOfferNoThrow(Bob, o4);
            offersCount++;
            // list of offers has to grow
            WALLET_CHECK(Alice.getOffersList().size() == offersCount);
            WALLET_CHECK(Bob.getOffersList().size() == offersCount);
            WALLET_CHECK(Cory.getOffersList().size() == offersCount);
            // amount of notifications has to be the same
            WALLET_CHECK(executionCount == 12);
        }
        
        {
            uint32_t execCount = 0;
            MockBoardObserver testObserver(
                [&execCount]
                (ChangeAction action, const vector<SwapOffer>& offers)
                {
                    execCount++;
                });
            Bob.Subscribe(&testObserver);
            {
                cout << "Case: no notification on new offer in status:" << endl;
                std::array<SwapOfferStatus,5> nonActiveStatuses {
                    SwapOfferStatus::InProgress,
                    SwapOfferStatus::Completed,
                    SwapOfferStatus::Canceled,
                    SwapOfferStatus::Expired,
                    SwapOfferStatus::Failed };

                for (auto s : nonActiveStatuses)
                {
                    SwapOffer o = correctOffer;
                    o.m_txId = ++txID;
                    cout << "\tparameter " << static_cast<uint32_t>(s) << endl;
                    o.m_status = s;
                    PublishOfferNoThrow(Alice, o);
                    WALLET_CHECK(Bob.getOffersList().size() == offersCount);
                }
                WALLET_CHECK(execCount == 0);
            }
            {
                cout << "Case: notification on new offer in Pending status" << endl;
                SwapOffer o = correctOffer;
                o.m_txId = ++txID;
                o.m_status = SwapOfferStatus::Pending;
                PublishOfferNoThrow(Alice, o);
                offersCount++;
                WALLET_CHECK(Bob.getOffersList().size() == offersCount);
                WALLET_CHECK(execCount == 1);
            }
            Bob.Unsubscribe(&testObserver);
        }
        cout << "Test end" << endl;
    }

    void TestLinkedTransactionChanges()
    {
        cout << endl << "Test linked transaction status changes" << endl;

        auto storage = createSqliteWalletDB();

        SwapOffer correctOffer;
        std::tie(correctOffer, std::ignore) = generateTestOffer(storage);
        TxID txID = correctOffer.m_txId;    // used to iterate and create unique ID's

        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouterA(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        BroadcastRouter broadcastRouterB(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());

        SwapOffersBoard Alice(broadcastRouterA, protocolHandler, storage);
        SwapOffersBoard Bob(broadcastRouterB, protocolHandler, storage);

        HeightHash startState;
        startState.m_Height = Fork1Height;
        Alice.onSystemStateChanged(startState);
        Bob.onSystemStateChanged(startState);

        size_t offerCount = 0;
        {
            cout << "Case: offers removed when Tx state changes to InProgress, Canceled, Failed" << endl;

            SwapOffer o1 = correctOffer;
            SwapOffer o2 = correctOffer;
            SwapOffer o3 = correctOffer;
            SwapOffer o4 = correctOffer;
            SwapOffer o5 = correctOffer;
            o1.m_txId = ++txID;
            o2.m_txId = ++txID;
            o3.m_txId = ++txID;
            o4.m_txId = ++txID;
            o5.m_txId = ++txID;
            PublishOfferNoThrow(Alice, o1);
            PublishOfferNoThrow(Alice, o2);
            PublishOfferNoThrow(Alice, o3);
            PublishOfferNoThrow(Alice, o4);
            PublishOfferNoThrow(Alice, o5);
            offerCount += 5;
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount);

            TxDescription tx1(o1.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(Fork1Height));
            TxDescription tx2(o2.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(Fork1Height));
            TxDescription tx3(o3.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(Fork1Height));
            TxDescription tx4(o4.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(Fork1Height));
            TxDescription tx5(o4.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(Fork1Height));
            TxDescription tx6(o4.m_txId, TxType::AtomicSwap, Amount(852), Amount(741), Height(Fork1Height));
            // this TxType is ignored
            TxDescription tx7(o4.m_txId, TxType::Simple, Amount(852), Amount(741), Height(Fork1Height));
            tx7.m_status = wallet::TxStatus::InProgress;
            // these states have to deactivate offer
            tx1.m_status = wallet::TxStatus::InProgress;
            tx2.m_status = wallet::TxStatus::Canceled;
            tx3.m_status = wallet::TxStatus::Failed;
            // these are ignored
            tx4.m_status = wallet::TxStatus::Pending;
            tx5.m_status = wallet::TxStatus::Completed;
            tx6.m_status = wallet::TxStatus::Registering;
            uint32_t exCount = 0;
            MockBoardObserver obsRemove([&exCount](ChangeAction action, const vector<SwapOffer>& offers) {
                        WALLET_CHECK(action == ChangeAction::Removed);
                        exCount++;
                    });
            Bob.Subscribe(&obsRemove);
            Alice.onTransactionChanged(ChangeAction::Updated, {tx5, tx4, tx1, tx3, tx2, tx6, tx7});
            Bob.Unsubscribe(&obsRemove);
            offerCount -= 3;
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount);
            WALLET_CHECK(exCount == 3);

            // cancel the remaining offers
            tx4.m_txId = o4.m_txId;
            tx4.m_status = wallet::TxStatus::Canceled;
            tx4.m_txType = TxType::AtomicSwap;
            tx5.m_txId = o5.m_txId;
            tx5.m_status = wallet::TxStatus::Canceled;
            tx5.m_txType = TxType::AtomicSwap;
            Alice.onTransactionChanged(ChangeAction::Updated, {tx4, tx5});
            offerCount -= 2;
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount);
            WALLET_CHECK(offerCount == 0);
        }

        {
            cout << "Case: offers removed when chain height growns beyond expiration" << endl;

            SwapOffer aliceOffer = correctOffer;
            SwapOffer aliceExpiredOffer = correctOffer;
            SwapOffer bobOffer = correctOffer;
            aliceOffer.m_txId = ++txID;
            aliceExpiredOffer.m_txId = ++txID;
            bobOffer.m_txId = ++txID;
            PublishOfferNoThrow(Bob, bobOffer);
            PublishOfferNoThrow(Alice, aliceOffer);
            offerCount += 2;

            WALLET_CHECK(Alice.getOffersList().size() == offerCount);
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);

            HeightHash expiredHeight, nonExpiredHeight;
            auto h = aliceOffer.GetParameter<Height>(TxParameterID::MinHeight);
            auto t = aliceOffer.GetParameter<Height>(TxParameterID::PeerResponseTime);
            expiredHeight.m_Height = *h + *t;
            nonExpiredHeight.m_Height = *h + *t - Height(1);

            uint32_t exCount = 0;
            MockBoardObserver obsRemove([&exCount](ChangeAction action, const vector<SwapOffer>& offers) {
                WALLET_CHECK(action == ChangeAction::Removed);
                WALLET_CHECK(offers.front().m_status == SwapOfferStatus::Expired);
                exCount++;
            });

            Bob.Subscribe(&obsRemove);
            Bob.onSystemStateChanged(nonExpiredHeight);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount);
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(exCount == 0);
            Bob.Unsubscribe(&obsRemove);

            Alice.Subscribe(&obsRemove);
            Alice.onSystemStateChanged(expiredHeight);
            Alice.Unsubscribe(&obsRemove);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount - 2);
            WALLET_CHECK(Bob.getOffersList().size() == offerCount);
            WALLET_CHECK(exCount == 2);

            // check expired offer 
            Alice.Subscribe(&obsRemove);
            PublishOfferNoThrow(Alice, aliceExpiredOffer);
            Alice.Unsubscribe(&obsRemove);
            WALLET_CHECK(Alice.getOffersList().size() == offerCount - 2);
            WALLET_CHECK(exCount == 2);
        }

        cout << "Test end" << endl;
    }

    void TestDelayedOfferUpdate()
    {
        cout << endl << "Test delayed offer update" << endl;

        auto storage = createSqliteWalletDB();

        SwapOffer correctOffer;
        std::tie(correctOffer, std::ignore) = generateTestOffer(storage);

        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouterA(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        BroadcastRouter broadcastRouterB(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());

        SwapOffersBoard Alice(broadcastRouterA, protocolHandler, storage);
        SwapOffersBoard Bob(broadcastRouterB, protocolHandler, storage);

        uint32_t exCount = 0;
        MockBoardObserver observer(
            [&exCount]
            (ChangeAction action, const vector<SwapOffer>& offers)
            {
                exCount++;
            });
        {
            cout << "Case: delayed offer update broadcast to network" << endl;
            // Case when no offer exist on board.
            // Transaction steps to states InProgress and Expired or other.
            // Board doesn't know if offer exits in network and doesn't broadcast status update.
            // Offer appear on board. Offer status update has to be broadcasted.
            SwapOffer o = correctOffer;
            TxDescription tx(o.m_txId, TxType::AtomicSwap, Amount(951), Amount(753), Height(654));
            
            tx.m_status = wallet::TxStatus::InProgress;
            Alice.Subscribe(&observer);
            Alice.onTransactionChanged(ChangeAction::Updated, {tx});
            WALLET_CHECK(exCount == 0);
            WALLET_CHECK(Alice.getOffersList().size() == 0);
            WALLET_CHECK(Bob.getOffersList().size() == 0);

            tx.m_status = wallet::TxStatus::Failed;
            Alice.onTransactionChanged(ChangeAction::Updated, {tx});
            WALLET_CHECK(exCount == 0);
            WALLET_CHECK(Alice.getOffersList().size() == 0);
            WALLET_CHECK(Bob.getOffersList().size() == 0);
            
            tx.m_status = wallet::TxStatus::Canceled;
            Alice.onTransactionChanged(ChangeAction::Updated, {tx});
            WALLET_CHECK(exCount == 0);
            WALLET_CHECK(Alice.getOffersList().size() == 0);
            WALLET_CHECK(Bob.getOffersList().size() == 0);

            PublishOfferNoThrow(Bob, o);
            WALLET_CHECK(exCount == 0);
            WALLET_CHECK(Alice.getOffersList().size() == 0);
            WALLET_CHECK(Bob.getOffersList().size() == 0);
        }
        cout << "Test end" << endl;
    }

    void TestOffersLifetimeCheck()
    {
        cout << endl << "Test offers lifetime check" << endl;

        auto storage = createSqliteWalletDB();

        SwapOffer correctOffer;
        std::tie(correctOffer, std::ignore) = generateTestOffer(storage);

        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouter(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        SwapOffersBoard Alice(broadcastRouter, protocolHandler, storage);

        HeightHash startState;
        startState.m_Height = Fork1Height;
        Alice.onSystemStateChanged(startState);

        {
            cout << "Case: offer lifetime is more 12h" << endl;
            SwapOffer o = correctOffer;
            o.SetParameter(TxParameterID::MinHeight, Fork1Height);
            o.SetParameter(TxParameterID::PeerResponseTime, Height(12*60 + 1));
            
            PublishOfferNoThrow(Alice, o);
            WALLET_CHECK(Alice.getOffersList().size() == 0);
        }
        {
            cout << "Case: offer lifetime is less 12h" << endl;
            SwapOffer o = correctOffer;
            o.SetParameter(TxParameterID::MinHeight, Fork1Height);
            o.SetParameter(TxParameterID::PeerResponseTime, Height(12*59));
            
            PublishOfferNoThrow(Alice, o);
            WALLET_CHECK(Alice.getOffersList().size() == 1);
        }
        cout << "Test end" << endl;
    }

    void TestOwnOfferCheck()
    {
        cout << endl << "Test own offer check based on addresses watching" << endl;

        auto storage = createSqliteWalletDB();

        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouter(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        SwapOffersBoard Alice(broadcastRouter, protocolHandler, storage);

        HeightHash startState;
        startState.m_Height = Fork1Height;
        Alice.onSystemStateChanged(startState);
        
        storage->Subscribe(&Alice);
        TxID txID;
        size_t offersOnBoard = 0;
        {
            cout << "Case: only own offers are published" << endl;

            SwapOffer correctOffer, foreignOffer, removedOffer;
            std::tie(correctOffer, std::ignore) = generateTestOffer(storage);
            std::tie(foreignOffer, std::ignore) = generateTestOffer(storage);
            std::tie(removedOffer, std::ignore) = generateTestOffer(storage);
            txID = correctOffer.m_txId;
            foreignOffer.m_txId = ++txID;
            removedOffer.m_txId = ++txID;

            storage->deleteAddress(foreignOffer.m_publisherId);
            PublishOfferNoThrow(Alice, foreignOffer);
            WALLET_CHECK(Alice.getOffersList().size() == offersOnBoard);

            storage->deleteAddress(removedOffer.m_publisherId);

            PublishOfferNoThrow(Alice, removedOffer);
            WALLET_CHECK(Alice.getOffersList().size() == offersOnBoard);

            PublishOfferNoThrow(Alice, correctOffer);
            WALLET_CHECK(Alice.getOffersList().size() == ++offersOnBoard);
            WALLET_CHECK(Alice.getOffersList()[0] == correctOffer);
        }
        {
            cout << "Case: own incoming offers correctly set" << endl;

            SwapOffer ownOffer, foreignOffer;
            uint64_t ownID, foreignID;
            std::tie(ownOffer, ownID) = generateTestOffer(storage);
            std::tie(foreignOffer, foreignID) = generateTestOffer(storage);
            ownOffer.m_txId = ++txID;
            foreignOffer.m_txId = ++txID;
            auto ownMsg = protocolHandler.createBroadcastMessage(ownOffer, ownID);
            auto foreignMsg = protocolHandler.createBroadcastMessage(foreignOffer, foreignID);
            storage->deleteAddress(foreignOffer.m_publisherId);

            uint32_t exCount = 0;
            MockBoardObserver observer(
                [&exCount, &ownOffer, &foreignOffer]
                (ChangeAction action, const vector<SwapOffer>& offers)
                {
                    WALLET_CHECK(action == ChangeAction::Added);
                    switch (exCount)
                    {
                    case 0:
                        WALLET_CHECK(offers[0].m_txId == ownOffer.m_txId);
                        WALLET_CHECK(offers[0].m_isOwn == true);
                        break;
                    case 1:
                        WALLET_CHECK(offers[0].m_txId == foreignOffer.m_txId);
                        WALLET_CHECK(offers[0].m_isOwn == false);
                    default:
                        break;
                    }
                    exCount++;
                });
            Alice.Subscribe(&observer);

            broadcastRouter.sendMessage(BroadcastContentType::SwapOffers, ownMsg);
            WALLET_CHECK(exCount == 1);
            WALLET_CHECK(Alice.getOffersList().size() == ++offersOnBoard);
            broadcastRouter.sendMessage(BroadcastContentType::SwapOffers, foreignMsg);
            WALLET_CHECK(exCount == 2);
            WALLET_CHECK(Alice.getOffersList().size() == ++offersOnBoard);
        }
    }

    void TestFillSwapTxParamsPublish()
    {
        cout << endl << "Test FillSwapTxParams produces publishable offer" << endl;

        auto storage = createSqliteWalletDB();

        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouter(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        SwapOffersBoard board(broadcastRouter, protocolHandler, storage);

        HeightHash startState;
        startState.m_Height = Fork1Height;
        board.onSystemStateChanged(startState);

        // The board learns about newly saved addresses via IWalletDbObserver;
        // subscribe so the publisher address created below is picked up
        // (mirrors how the real wallet client wires SwapOffersBoard).
        storage->Subscribe(&board);

        // Create the offer exactly as UI/CLI/API do: via FillSwapTxParams.
        auto params = CreateSwapTransactionParameters(generateTxID());
        FillSwapTxParams(&params,
                         *storage,
                         Fork1Height,          // minHeight
                         1000,                 // amount
                         100,                  // beamFee
                         AtomicSwapCoin::Bitcoin,
                         2000,                 // swapAmount
                         10,                   // swapFeeRate
                         true);                // isBeamSide

        SwapOffer offer(*params.GetTxID());
        offer.SetTxParameters(params.Pack());
        offer.m_status = SwapOfferStatus::Pending;
        offer.m_coin = AtomicSwapCoin::Bitcoin;
        offer.m_publisherId = *params.GetParameter<WalletID>(TxParameterID::MyAddr);

        size_t offersReceived = 0;
        MockBoardObserver observer([&offersReceived](ChangeAction action, const vector<SwapOffer>& offers) {
            if (action == ChangeAction::Added) offersReceived += offers.size();
        });
        board.Subscribe(&observer);

        // A publisher address that was never saved makes the board reject the
        // offer as foreign ("Offer has foreign Pk and will not be published").
        WALLET_CHECK_NO_THROW(board.publishOffer(offer));
        WALLET_CHECK(offersReceived == 1);

        board.Unsubscribe(&observer);
        storage->Unsubscribe(&board);
    }

    void TestMirrorAndTokenizeCarryTokenParams()
    {
        cout << endl << "Test MirrorSwapTxParams/PrepareSwapTxParamsForTokenization carry Erc20 token params" << endl;

        // MirrorSwapTxParams and PrepareSwapTxParamsForTokenization must carry
        // AtomicSwapTokenContract/Symbol/Decimals and AtomicSwapBeamAssetID/Name
        // through unchanged, or accept/publish-offer round trips silently drop
        // the extended-offer data.
        auto storage = createSqliteWalletDB();

        auto params = CreateSwapTransactionParameters(generateTxID());
        FillSwapTxParams(&params,
                         *storage,
                         Fork1Height,           // minHeight
                         1000,                  // amount
                         100,                   // beamFee
                         AtomicSwapCoin::Erc20Token,
                         2000,                  // swapAmount
                         10,                    // swapFeeRate
                         true);                 // isBeamSide

        const std::string kContract = "0x000000000000000000000000000000000000ab";
        const std::string kSymbol = "TKN";
        const uint8_t kDecimals = 6;
        const Asset::ID kAssetId = 7;
        const std::string kAssetName = "TEST";

        params.SetParameter(TxParameterID::AtomicSwapTokenContract, kContract);
        params.SetParameter(TxParameterID::AtomicSwapTokenSymbol, kSymbol);
        params.SetParameter(TxParameterID::AtomicSwapTokenDecimals, kDecimals);
        params.SetParameter(TxParameterID::AtomicSwapBeamAssetID, kAssetId);
        params.SetParameter(TxParameterID::AtomicSwapBeamAssetName, kAssetName);

        auto checkTokenParamsSurvived = [&](const TxParameters& result, const char* stage)
        {
            cout << "\tStage: " << stage << endl;
            auto contract = result.GetParameter<std::string>(TxParameterID::AtomicSwapTokenContract);
            auto symbol = result.GetParameter<std::string>(TxParameterID::AtomicSwapTokenSymbol);
            auto decimals = result.GetParameter<uint8_t>(TxParameterID::AtomicSwapTokenDecimals);
            auto assetId = result.GetParameter<Asset::ID>(TxParameterID::AtomicSwapBeamAssetID);
            auto assetName = result.GetParameter<std::string>(TxParameterID::AtomicSwapBeamAssetName);

            WALLET_CHECK(contract && *contract == kContract);
            WALLET_CHECK(symbol && *symbol == kSymbol);
            WALLET_CHECK(decimals && *decimals == kDecimals);
            WALLET_CHECK(assetId && *assetId == kAssetId);
            WALLET_CHECK(assetName && *assetName == kAssetName);
        };

        auto mirrored = MirrorSwapTxParams(params, true);
        checkTokenParamsSurvived(mirrored, "MirrorSwapTxParams");

        auto tokenized = PrepareSwapTxParamsForTokenization(params);
        checkTokenParamsSurvived(tokenized, "PrepareSwapTxParamsForTokenization");

        cout << "Test end" << endl;
    }

    void TestClassicOfferWireStability()
    {
        cout << endl << "Test classic offer wire (SwapOfferToken) byte stability" << endl;

        // Locks the wire format that old wallets parse for a classic
        // (non-extended) offer's m_coin/params.
        //
        // Two non-obvious facts shape this test:
        //   - createBroadcastMessage's signature is NOT deterministic: ECC::
        //     SignatureBase::CreateNonces (core/ecc.cpp) mixes GenRandom() into
        //     the nonce, so msg.m_signature differs between calls even for the
        //     identical SwapOffer. msg.m_content (== toByteBuffer(SwapOfferToken
        //     (offer))) is stable though, so the golden asserts on
        //     SwapOfferToken's serialized content rather than the signed
        //     BroadcastMsg.
        //   - A DB-derived WalletID (via generateTestOffer/createSqliteWalletDB)
        //     is also not run-to-run stable: WalletDB::AllocateKidRange
        //     (wallet/core/wallet_db.cpp) seeds the 'LastKid' counter from
        //     beam::getTimestamp() when the DB has no prior value, so the
        //     publisher WalletID's m_Channel differs on every fresh-DB run.
        //     The golden below therefore uses a hardcoded TxID/WalletID instead
        //     of generateTestOffer's DB-backed address.
        //
        // If the serialization format ever legitimately changes, regenerate by
        // temporarily printing the actual hex here and pasting it back in.
        TxID txID;
        for (uint8_t i = 0; i < 16; ++i) txID[i] = i + 1;

        WalletID publisherId;
        publisherId.m_Channel = 0x1122334455667788ULL;
        for (uint8_t i = 0; i < 32; ++i) publisherId.m_Pk.m_pData[i] = i + 1;

        SwapOffer offer(txID, SwapOfferStatus::Pending, publisherId, AtomicSwapCoin::Bitcoin, true);
        offer.SetParameter(TxParameterID::AtomicSwapCoin, offer.m_coin);
        offer.SetParameter(TxParameterID::AtomicSwapIsBeamSide, true);
        offer.SetParameter(TxParameterID::Amount, Amount(12345));
        offer.SetParameter(TxParameterID::AtomicSwapAmount, Amount(6789));
        offer.SetParameter(TxParameterID::MinHeight, Height(Fork1Height));
        offer.SetParameter(TxParameterID::PeerResponseTime, Height(10));
        offer.SetParameter(TxParameterID::TransactionType, TxType::AtomicSwap);

        const std::string kGoldenHex =
            "010102030405060708090a0b0c0d0e0f1001800111223344556677880102030405"
            "060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20014087010082"
            "01010102830239300104818a0118818a011e8101011f814001208302851a";

        ByteBuffer tokenBytes = toByteBuffer(SwapOfferToken(offer));
        const std::string actualHex = to_hex(tokenBytes.data(), tokenBytes.size());
        WALLET_CHECK(actualHex == kGoldenHex);

        // Same offer serialized twice must also be stable (rules out any
        // hidden per-call nondeterminism in SwapOfferToken/Pack itself).
        ByteBuffer tokenBytes2 = toByteBuffer(SwapOfferToken(offer));
        WALLET_CHECK(tokenBytes == tokenBytes2);

        cout << "Test end" << endl;
    }

    void TestExtendedOfferCompat()
    {
        cout << endl << "Test extended-offer (CA / Erc20) wire compat and old-wallet-guard drop" << endl;

        // An unmodified old wallet's onOfferFromNetwork rejects any offer whose
        // m_coin is >= the old AtomicSwapCoin::Unknown ordinal, which was 10
        // (see common.h's static_asserts: Erc20Token now takes that old ordinal,
        // Unknown moved to 12). ExtendedOffer's ordinal is 11: >= 10 (old Unknown)
        // so the old guard drops it; but see below re. >= 12 being false.
        constexpr int32_t kOldUnknownOrdinal = 10;
        static_assert(static_cast<int32_t>(AtomicSwapCoin::ExtendedOffer) >= kOldUnknownOrdinal,
            "an older peer's 'm_coin >= old Unknown (10)' guard must drop ExtendedOffer (11), "
            "which is why the board wire-tags extended offers with it instead of the real coin");
        static_assert(!(static_cast<int32_t>(AtomicSwapCoin::ExtendedOffer) >= 12),
            "ExtendedOffer (11) must stay below the NEW Unknown ordinal (12): it's a valid, "
            "intentionally-produced wire value on the new wallet side, not itself an unknown coin");

        auto storage = createSqliteWalletDB();
        WalletAddress wa;
        storage->createAddress(wa);
        storage->saveAddress(wa);

        OfferBoardProtocolHandler protocolHandler(storage->get_SbbsKdf());
        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter routerAlice(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        BroadcastRouter routerBob(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        BroadcastRouter routerEve(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());

        SwapOffersBoard Alice(routerAlice, protocolHandler, storage);
        SwapOffersBoard Bob(routerBob, protocolHandler, storage);

        HeightHash startState;
        startState.m_Height = Fork1Height;
        Alice.onSystemStateChanged(startState);
        Bob.onSystemStateChanged(startState);

        // Eve doesn't run a SwapOffersBoard; she just snoops the raw broadcast
        // traffic on the SwapOffers content type to observe the actual wire
        // m_coin Alice's board puts on the message (as opposed to Bob's already-
        // resolved, post-onOfferFromNetwork copy).
        boost::optional<SwapOffer> lastWireOffer;
        MockBroadcastListener eve(
            [&lastWireOffer, &protocolHandler]
            (BroadcastMsg& msg)
            {
                lastWireOffer = protocolHandler.parseMessage(msg);
            });
        routerEve.registerListener(BroadcastContentType::SwapOffers, &eve);

        auto findByTxId = [](const SwapOffersBoard& board, const TxID& txId) -> boost::optional<SwapOffer>
        {
            for (auto& o : board.getOffersList())
            {
                if (o.m_txId == txId)
                {
                    return o;
                }
            }
            return boost::none;
        };

        // generateTxID() reseeds std::rand from time(nullptr) (1s resolution)
        // on every call, so two calls within the same wall-clock second produce
        // an identical TxID and the second publishOffer collides with the first
        // (OfferAlreadyPublishedException) - use one base TxID and increment for
        // the second case instead of calling generateTxID() twice.
        TxID txID = generateTxID();

        {
            cout << "\tCase: CA offer (AtomicSwapBeamAssetID=7, foreign coin Bitcoin)" << endl;

            SwapOffer offer = createOffer(txID, SwapOfferStatus::Pending, wa.m_BbsAddr, AtomicSwapCoin::Bitcoin, true);
            offer.SetParameter(TxParameterID::AtomicSwapBeamAssetID, Asset::ID(7));
            offer.SetParameter(TxParameterID::AtomicSwapBeamAssetName, std::string("MyCoolAsset"));

            lastWireOffer.reset();
            PublishOfferNoThrow(Alice, offer);

            // Wire assertion: what actually went out on the wire is ExtendedOffer,
            // never the real (Bitcoin) coin.
            WALLET_CHECK(lastWireOffer);
            WALLET_CHECK(lastWireOffer->m_coin == AtomicSwapCoin::ExtendedOffer);

            // Receive-side assertion: Bob's cached/observed copy resolves back to
            // the real foreign coin, with the CA params intact.
            auto received = findByTxId(Bob, offer.m_txId);
            WALLET_CHECK(received);
            WALLET_CHECK(received->ResolveCoin() == AtomicSwapCoin::Bitcoin);
            auto beamAssetId = received->GetParameter<Asset::ID>(TxParameterID::AtomicSwapBeamAssetID);
            WALLET_CHECK(beamAssetId && *beamAssetId == Asset::ID(7));
            auto beamAssetName = received->GetParameter<std::string>(TxParameterID::AtomicSwapBeamAssetName);
            WALLET_CHECK(beamAssetName && *beamAssetName == "MyCoolAsset");
        }
        {
            cout << "\tCase: Erc20 extended offer (foreign coin Erc20Token)" << endl;

            SwapOffer offer = createOffer(++txID, SwapOfferStatus::Pending, wa.m_BbsAddr, AtomicSwapCoin::Erc20Token, true);
            offer.SetParameter(TxParameterID::AtomicSwapTokenContract, std::string("0x0000000000000000000000000000000000000002"));
            offer.SetParameter(TxParameterID::AtomicSwapTokenSymbol, std::string("XYZ"));
            offer.SetParameter(TxParameterID::AtomicSwapTokenDecimals, uint8_t(6));

            lastWireOffer.reset();
            PublishOfferNoThrow(Alice, offer);

            WALLET_CHECK(lastWireOffer);
            WALLET_CHECK(lastWireOffer->m_coin == AtomicSwapCoin::ExtendedOffer);

            auto received = findByTxId(Bob, offer.m_txId);
            WALLET_CHECK(received);
            WALLET_CHECK(received->ResolveCoin() == AtomicSwapCoin::Erc20Token);
            auto contract = received->GetParameter<std::string>(TxParameterID::AtomicSwapTokenContract);
            WALLET_CHECK(contract && *contract == "0x0000000000000000000000000000000000000002");
            auto symbol = received->GetParameter<std::string>(TxParameterID::AtomicSwapTokenSymbol);
            WALLET_CHECK(symbol && *symbol == "XYZ");
            auto decimals = received->GetParameter<uint8_t>(TxParameterID::AtomicSwapTokenDecimals);
            WALLET_CHECK(decimals && *decimals == uint8_t(6));
        }

        routerEve.unregisterListener(BroadcastContentType::SwapOffers);

        cout << "Test end" << endl;
    }

} // namespace

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main()
{
    cout << "SwapOffersBoard tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    beam::Rules r;
    beam::Rules::Scope scopeRules(r);

    r.m_Consensus = Rules::Consensus::FakePoW;
    r.UpdateChecksum();
    r.pForks[1].m_Height = Fork1Height;
    r.pForks[2].m_Height = Fork2Height;

    TestProtocolHandlerSignature();
    TestProtocolHandlerIntegration();

    TestMandatoryParameters();
    TestRawErc20WireCoinRejected();
    TestExtendedErc20OfferDecimalsBounded();
    TestClassicOfferWireStability();
    TestExtendedOfferCompat();
    TestCommunication();
    TestLinkedTransactionChanges();
    TestDelayedOfferUpdate();
    TestOffersLifetimeCheck();
    TestOwnOfferCheck();
    TestFillSwapTxParamsPublish();
    TestMirrorAndTokenizeCarryTokenParams();

    boost::filesystem::remove(dbFileName);

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
