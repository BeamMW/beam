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

#pragma once
#include <limits>
#include "ecc_native.h"
#include "lelantus.h"
#include "merkle.h"
#include "difficulty.h"
#include "../utility/executor.h"

namespace beam
{
	class IExternalPOW;

	const Height MaxHeight = std::numeric_limits<Height>::max();
	const uint8_t MaxPrivacyAnonimitySetFractionsCount = 64;
	const uint8_t kIsSelfTxBit = 0b00000001;

	struct PeerID :public ECC::uintBig
	{
		using ECC::uintBig::uintBig;
		using ECC::uintBig::operator =;

		bool ExportNnz(ECC::Point::Native&) const;
		bool Import(const ECC::Point::Native&); // returns if the sign is preserved
		bool FromSk(ECC::Scalar::Native&); // will negate the scalar iff necessary. returns if the sign is preserved
	};

	typedef uint64_t BbsChannel;
	typedef ECC::Hash::Value BbsMsgID;
	typedef uint64_t TxoID;

	using ECC::Key;

	typedef ECC::uintBig ContractID;

	namespace MasterKey
	{
		Key::IKdf::Ptr get_Child(Key::IKdf&, Key::Index);
	}

	Timestamp getTimestamp();
	uint32_t GetTime_ms(); // platform-independent GetTickCount
	uint32_t GetTimeNnz_ms(); // guaranteed non-zero

	struct ILongAction
	{
		virtual void Reset(const char*, uint64_t nTotal) = 0;
		virtual void SetTotal(uint64_t nTotal) = 0;
		virtual bool OnProgress(uint64_t pos) = 0;
	};
	struct LongAction : ILongAction
	{
		uint32_t m_Last_ms = 0;
		uint64_t m_Total = 0;
		ILongAction *m_pExternal = nullptr;

		LongAction(const char* sz, uint64_t nTotal, ILongAction *pExternal = nullptr)
			: m_pExternal(pExternal)
		{
			Reset(sz, nTotal);
		}
		LongAction() = default;

		void Reset(const char*, uint64_t nTotal) final;
		void SetTotal(uint64_t nTotal) final;
		bool OnProgress(uint64_t pos) final;
	};

	void HeightAdd(Height& trg, Height val); // saturates if overflow

	struct HeightRange
	{
		// Convention: inclusive, i.e. both endings are part of the range.
		// m_Min == m_Max means the range includes a single height. Therefore (m_Min > m_Max) is NOT invalid, it just denotes an empty range.
		Height m_Min;
		Height m_Max;

		HeightRange() {
			Reset();
		}

		HeightRange(Height h0, Height h1) {
			m_Min = h0;
			m_Max = h1;
		}

		HeightRange(Height h) {
			m_Min = m_Max = h;
		}

		void Reset();
		void Intersect(const HeightRange&);

		bool IsEmpty() const;
		bool IsInRange(Height) const;
		bool IsInRangeRelative(Height) const; // assuming m_Min was already subtracted
	};

	namespace AmountBig
	{
		typedef uintBig_t<sizeof(Amount) + sizeof(Height)> Type; // 128 bits
		Amount get_Lo(const Type&);
		Amount get_Hi(const Type&);

		void AddTo(ECC::Point::Native&, const Type&);

		// This one is not optimized (slow)
		void AddTo(ECC::Point::Native& res, const Type& x, const ECC::Point::Native& hGen);

		void Print(std::ostream&, const Type&, bool bTrim = true);
		void Print(std::ostream&, Amount, bool bTrim = true);

		template <typename T>
		struct Printable {
			const T& m_Val;
			Printable(const T& x) :m_Val(x) {}

			friend std::ostream& operator << (std::ostream& os, const AmountBig::Printable<T>& x) {
				AmountBig::Print(os, x.m_Val);
				return os;
			}
		};
	};

	typedef int64_t AmountSigned;
	static_assert(sizeof(Amount) == sizeof(AmountSigned), "");

	struct HeightHash
	{
		Merkle::Hash	m_Hash;
		Height			m_Height;

		int cmp(const HeightHash&) const;
		COMPARISON_VIA_CMP
	};

	std::ostream& operator << (std::ostream&, const HeightHash&);

    struct HeightPos
    {
        Height m_Height = 0;
        uint32_t m_Pos = 0;

		HeightPos() = default;
		HeightPos(Height h, uint32_t pos = 0)
			:m_Height(h)
			,m_Pos(pos)
		{}

        template <typename Archive>
        void serialize(Archive& ar)
        {
            ar
                & m_Height
                & m_Pos;
        }
    };

	struct Asset
	{
		typedef uint32_t ID; // 1-based asset index. 0 is reserved for default asset (Beam)
		static constexpr ID s_MaxCount  = uint32_t(1) << 30; // 1 billion. Of course practically it'll be very much smaller
		static constexpr ID s_InvalidID = 0;
		static constexpr ID s_BeamID    = 0;
		static const PeerID s_InvalidOwnerID;

		struct Base
		{
			ID m_ID;

			explicit Base(ID id = s_InvalidID) :m_ID(id) {}

			void get_Generator(ECC::Point::Native&) const;
			void get_Generator(ECC::Point::Storage&) const;
			void get_Generator(ECC::Point::Native&, ECC::Point::Storage&) const;

			void get_GeneratorSafe(ECC::Point::Storage&) const; // works for aid=0
			static const ECC::Point::Compact& get_H();
		};

		struct Metadata
		{
			ByteBuffer m_Value;
			ECC::Hash::Value m_Hash = Zero; // not serialized

			void set_String(const std::string&, bool bLegacy);
			void get_String(std::string&) const;

			void Reset();
			void UpdateHash(); // called automatically during deserialization
			void get_Owner(PeerID&, Key::IPKdf&) const;
			void get_Owner(PeerID&, const ContractID&) const;
		};

		struct CreateInfo
		{
			PeerID m_Owner = Zero;
			ContractID m_Cid = Zero;
			Amount m_Deposit = 0;
			Metadata m_Metadata;
			static const uint32_t s_MetadataMaxSize = 1024 * 16; // 16K

			void SetCid(const ContractID*);
			bool Recognize(Key::IPKdf&) const;
			bool IsDefDeposit() const;
		};

		struct Info
			:public CreateInfo
		{
			AmountBig::Type m_Value = Zero;
			Height m_LockHeight = 0; // last emitted/burned change height. if emitted atm - when was latest 1st emission. If burned atm - what was last burn.

			void Reset();
			bool IsEmpty() const;
			bool IsValid() const;
		};

		struct Full
			:public Base
			,public Info
		{
			void get_Hash(ECC::Hash::Value&) const;
		};

		struct Proof
			:public Sigma::Proof
		{
			typedef std::unique_ptr<Proof> Ptr;

			struct Params
			{
				static Asset::ID s_AidMax_Global;
				static thread_local Asset::ID s_AidMax_Override;

				static Asset::ID Make(Asset::ID aid, bool bHideAlways) {
					return aid ? aid : bHideAlways ? 1 : 0;
				}

				static Asset::ID get_AidMax(Height hScheme); // returns 0 if no need to hide
				static Asset::ID get_AidMax();

				static bool IsNeeded(Asset::ID aid, Height hScheme) {
					return aid || get_AidMax(hScheme);
				}

				struct Override
				{
					Asset::ID m_Prev;

					Override(Asset::ID aid)
					{
						m_Prev = s_AidMax_Override;
						s_AidMax_Override = aid + 1;

					}
					~Override()
					{
						s_AidMax_Override = m_Prev;
					}
				};
			};

			Asset::ID m_Begin; // 1st element
			ECC::Point m_hGen;

			bool IsValid(Height, ECC::Point::Native& hGen) const; // for testing only, in real-world cases batch verification should be used!
			bool IsValidPrepare(ECC::Point::Native& hGen, ECC::InnerProduct::BatchContext& bc, ECC::Scalar::Native* pKs) const;
			void Create(Height, ECC::Point::Native& genBlinded, ECC::Scalar::Native& skInOut, Amount val, Asset::ID, const ECC::Point::Native& gen, const ECC::Hash::Value* phvSeed = nullptr);
			void Create(Height, ECC::Point::Native& genBlinded, ECC::Scalar::Native& skInOut, Amount val, Asset::ID, const ECC::Hash::Value* phvSeed = nullptr);
			void Create(Height, ECC::Point::Native& genBlinded, const ECC::Scalar::Native& skGen, Asset::ID, const ECC::Point::Native& gen);

			static void ModifySk(ECC::Scalar::Native& skInOut, const ECC::Scalar::Native& skGen, Amount val);

			static void Expose(ECC::Oracle&, Height hScheme, const Ptr&);

			void Clone(Ptr&) const;

			struct BatchContext
			{
				static thread_local BatchContext* s_pInstance;

				struct Scope
				{
					BatchContext* m_pPrev;

					Scope(BatchContext& bc) {
						m_pPrev = s_pInstance;
						s_pInstance = &bc;
					}
					~Scope() {
						s_pInstance = m_pPrev;
					}
				};

				virtual bool IsValid(Height, ECC::Point::Native& hGen, const Proof&) = 0;
			};

		private:
			struct CmList;
		};
	};

	struct Rules
	{
		Rules();
		static Rules& get();

		struct Scope {
			const Rules* m_pPrev;
			Scope(const Rules&);
			~Scope();
		};

		static const Height HeightGenesis; // height of the 1st block, defines the convention. Currently =1
		static constexpr Amount Coin = 100000000; // how many quantas in a single coin. Just cosmetic, has no meaning to the processing (which is in terms of quantas)

#define RulesNetworks(macro) \
		macro(mainnet) \
		macro(masternet) \
		macro(testnet) \
		macro(dappnet)

		enum struct Network
		{
#define THE_MACRO(name) name,
			RulesNetworks(THE_MACRO)
#undef THE_MACRO

		} m_Network;

		const char* get_NetworkName() const;

		struct {
			// emission parameters
			Amount Value0;
			Height Drop0;
			Height Drop1;
		} Emission;

		struct {
			Height Coinbase;
			Height Std;
		} Maturity;

		struct {
			// timestamp & difficulty.
			uint32_t Target_s;
			uint32_t WindowWork;
			uint32_t MaxAhead_s;
			uint32_t WindowMedian0;
			uint32_t WindowMedian1;
			Difficulty Difficulty0;

			struct {
				// damp factor. Adjustment of actual dt toward expected, effectively dampens
				uint32_t M;
				uint32_t N;
			} Damp;
		} DA;

		struct {
			bool Enabled;
			Amount DepositForList2;
			Amount DepositForList5;
			Height LockPeriod;
			Sigma::Cfg m_ProofCfg;
		} CA;

		uint32_t MaxRollback;

		size_t MaxBodySize;

		bool AllowPublicUtxos;
		bool FakePoW;

		Height MaxKernelValidityDH; // past Fork2
		// if kernel has higher lifetime - its max height is implicitly decreased

		ECC::Hash::Value Prehistoric; // Prev hash of the 1st block
		ECC::Hash::Value TreasuryChecksum;

		struct {
			bool Enabled = true; // past Fork2

			Sigma::Cfg m_ProofMax;
			Sigma::Cfg m_ProofMin;

			// Max distance of the specified window from the tip where the prover is allowed to use m_ProofMax.
			// For proofs with bigger distance only m_ProofMin is supported
			uint32_t MaxWindowBacklog;
			// Hence "big" proofs won't need more than 128K most recent elements

			// max shielded ins/outs per block
			uint32_t MaxIns; // input processing is heavy
			uint32_t MaxOuts; // dust protection

		} Shielded;

		struct
		{
			uint32_t v0;
			uint32_t v2;
			bool IsTestnet;
		} Magic;

		void SetNetworkParams();
		void UpdateChecksum();

		static Amount get_Emission(Height);
		static void get_Emission(AmountBig::Type&, const HeightRange&);
		static void get_Emission(AmountBig::Type&, const HeightRange&, Amount base);

		HeightHash pForks[7];

		const HeightHash& get_LastFork() const;
		const HeightHash* FindFork(const Merkle::Hash&) const;
		uint32_t FindFork(Height) const;
		Height get_ForkMaxHeightSafe(uint32_t iFork) const;
		void DisableForksFrom(uint32_t);
		std::string get_SignatureStr() const;
		Amount get_DepositForCA(Height hScheme) const;
		bool IsEnabledCA(Height hScheme) const;

		static void Fail_Fork(uint32_t iFork);

		void TestForkAtLeast(Height h, uint32_t iFork) const
		{
			assert(iFork < _countof(pForks));
			if (h < pForks[iFork].m_Height)
				Fail_Fork(iFork);
		}

		void TestEnabledCA() const
		{
			if (!CA.Enabled)
				Exc::Fail("CA disabled");
		}

		void TestEnabledShielded() const
		{
			if (!Shielded.Enabled)
				Exc::Fail("Shielded disabled");
		}

	private:
		Amount get_EmissionEx(Height, Height& hEnd, Amount base) const;
		bool IsForkHeightsConsistent() const;
	};

	class ExecutorMT_R
		:public ExecutorMT
	{
		virtual void StartThread(MyThread&, uint32_t iThread) override;
		void RunThreadInternal(uint32_t iThread, const Rules&);
		virtual void RunThread(uint32_t iThread);

	};

	struct CoinID
		:public Key::ID
	{
		struct Scheme
		{
			static const uint8_t V0 = 0;
			static const uint8_t V1 = 1;
			static const uint8_t BB21 = 2; // worakround for BB.2.1
			static const uint8_t V3 = 3;
			static const uint8_t V_Miner0 = 4;

			static const uint32_t s_SubKeyBits = 24;
			static const Key::Index s_SubKeyMask = (static_cast<Key::Index>(1) << s_SubKeyBits) - 1;
		};

		Amount m_Value;

		Asset::ID m_AssetID = 0;

		CoinID() = default;
		CoinID(Zero_)
			:ID(Zero)
			,m_Value(0)
		{
			set_Subkey(0);
		}

		CoinID(Amount v, uint64_t nIdx, Key::Type type, Key::Index nSubIdx = 0)
			:ID(nIdx, type)
			,m_Value(v)
		{
			set_Subkey(nSubIdx, Scheme::V3);
		}

		Key::Index get_Scheme() const
		{
			return m_SubIdx >> Scheme::s_SubKeyBits;
		}

		Key::Index get_Subkey() const
		{
			return m_SubIdx & Scheme::s_SubKeyMask;
		}

		void set_Subkey(Key::Index nSubIdx, Key::Index nScheme = Scheme::V3)
		{
			m_SubIdx = (nSubIdx & Scheme::s_SubKeyMask) | (nScheme << Scheme::s_SubKeyBits);
		}

		bool IsBb21Possible() const
		{
			return m_SubIdx && (Scheme::V0 == get_Scheme());
		}

		void set_WorkaroundBb21()
		{
			set_Subkey(get_Subkey(), Scheme::BB21);
		}

		bool IsWorkaroundMiner0Possible() const
		{
			return (Scheme::V3 == get_Scheme()) && !get_Subkey();
		}

		void set_WorkaroundMiner0()
		{
			set_Subkey(0, Scheme::V_Miner0);
		}

		void get_Hash(ECC::Hash::Value&) const;

		bool get_ChildKdfIndex(Key::Index&) const; // returns false if chils is not needed
		Key::IKdf::Ptr get_ChildKdf(const Key::IKdf::Ptr& pMasterKdf) const;

		bool IsDummy() const
		{
			return
				!m_Value &&
				!m_AssetID &&
				(Key::Type::Decoy == m_Type);
		}

		struct Generator
		{
			ECC::Point::Native m_hGen;
			Generator(Asset::ID);
			void AddValue(ECC::Point::Native& comm, Amount) const;
		};

		class Worker
			:public Generator
		{
			static void get_sk1(ECC::Scalar::Native& res, const ECC::Point::Native& comm0, const ECC::Point::Native& sk0_J);
			void CreateInternal(ECC::Scalar::Native&, ECC::Point::Native&, bool bComm, Key::IKdf& kdf) const;

		public:
			const CoinID& m_Cid;

			Worker(const CoinID&);

			void AddValue(ECC::Point::Native& comm) const;

			void Create(ECC::Scalar::Native& sk, Key::IKdf&) const;
			void Create(ECC::Scalar::Native& sk, ECC::Point::Native& comm, Key::IKdf&) const;
			void Create(ECC::Scalar::Native& sk, ECC::Point& comm, Key::IKdf&) const;

			void Recover(ECC::Point::Native& comm, Key::IPKdf&) const;
			void Recover(ECC::Point::Native& pkG_in_res_out, const ECC::Point::Native& pkJ) const;
		};
	};

	std::ostream& operator << (std::ostream&, const CoinID&);

	struct TxStats
	{
		AmountBig::Type m_Fee;
		AmountBig::Type m_Coinbase;

		uint32_t m_Kernels;
		uint32_t m_KernelsNonStd;
		uint32_t m_Inputs; // MW only
		uint32_t m_Outputs; // MW only
		uint32_t m_InputsShielded;
		uint32_t m_OutputsShielded;
		uint32_t m_Contract;
		uint32_t m_ContractSizeExtra;

		TxStats() { Reset(); }

		void Reset();
		void operator += (const TxStats&);
	};

	struct TxElement
	{
		ECC::Point m_Commitment;
		int cmp(const TxElement&) const;
	};

	struct Input
		:public TxElement
	{
		// used internally. Not serialized/transferred
		struct Internal
		{
			Height m_Maturity = 0; // of the consumed (being-spent) UTXO
			TxoID m_ID = 0;
		} m_Internal;

		typedef std::unique_ptr<Input> Ptr;
		typedef uint32_t Count; // the type for count of duplicate UTXOs in the system

		struct State
		{
			Height m_Maturity;
			Input::Count m_Count;

			void get_ID(Merkle::Hash&, const ECC::Point&) const;

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_Maturity
					& m_Count;
			}
		};

		struct Proof
		{
			State m_State;
			Merkle::Proof m_Proof;

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_State
					& m_Proof;
			}

			static const uint32_t s_EntriesMax = 20; // if this is the size of the vector - the result is probably trunacted
		};

		Input() = default;
		Input(const Input& v)
			:TxElement(v)
			, m_Internal(v.m_Internal)
		{
		}
		Input(Input&& v) noexcept
			:TxElement(std::move(v))
			,m_Internal(std::exchange(v.m_Internal, {}))
		{
		}

		void AddStats(TxStats&) const;

		Input& operator = (const Input&);
		Input& operator = (Input&&) noexcept;
		COMPARISON_VIA_CMP
	};

	inline bool operator < (const Input::Ptr& a, const Input::Ptr& b) { return *a < *b; }

	struct Output
		:public TxElement
	{
		typedef std::unique_ptr<Output> Ptr;

		bool		m_Coinbase;
		Height		m_Incubation; // # of blocks before it's mature

		Output()
			:m_Coinbase(false)
			,m_Incubation(0)
		{
		}

		Output(Output&& o)
			:TxElement(o)
			,m_Coinbase(o.m_Coinbase)
			,m_Incubation(o.m_Incubation)
			,m_pConfidential(std::move(o.m_pConfidential))
			,m_pPublic(std::move(o.m_pPublic))
			,m_pAsset(std::move(o.m_pAsset))
		{
		}

		struct User
		{
			ECC::Scalar m_pExtra[2];
			User()
			{
				ZeroObject(m_pExtra);
			}
#pragma pack (push, 1)
			struct Packed
			{
				typedef uintBig_t<16> TxID;
				Amount m_Fee;
				Amount m_Amount;
				TxID m_TxID;
				PeerID m_Peer;
			};
#pragma pack (pop)
			static Packed* ToPacked(User& user)
			{
				return reinterpret_cast<Packed*>(user.m_pExtra);
			}

			static const Packed* ToPacked(const User& user)
			{
				return reinterpret_cast<const Packed*>(user.m_pExtra);
			}

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar & m_pExtra;
			}
		};

		static const Amount s_MinimumValue = 1;

		// one of the following *must* be specified
		std::unique_ptr<ECC::RangeProof::Confidential>	m_pConfidential;
		std::unique_ptr<ECC::RangeProof::Public>		m_pPublic;
		Asset::Proof::Ptr								m_pAsset;

		struct OpCode {
			enum Enum {
				Standard,
				Public, // insist on public rangeproof, regardless to m_Coinbase. For tests only
				Mpc_1, // ignore coinKdf, generate rangeproof without sk, up to T1/T2
				Mpc_2, // Finish rangeproof after T1/T2 and TauX were updated by the peer
			};
		};

		void Create(Height hScheme, ECC::Scalar::Native&, Key::IKdf& coinKdf, const CoinID&, Key::IPKdf& tagKdf, OpCode::Enum = OpCode::Standard, const User* = nullptr);

		bool Recover(Height hScheme, Key::IPKdf& tagKdf, CoinID&, User* = nullptr) const;
		bool VerifyRecovered(Key::IPKdf& coinKdf, const CoinID&) const;

		bool IsValid(Height hScheme, ECC::Point::Native& comm) const;
		Height get_MinMaturity(Height h) const; // regardless to the explicitly-overridden

		void AddStats(TxStats&) const;

		Output& operator = (const Output&);
		int cmp(const Output&) const;
		COMPARISON_VIA_CMP

		static void GenerateSeedKid(ECC::uintBig&, const ECC::Point& comm, Key::IPKdf&);
		void Prepare(ECC::Oracle&, Height hScheme) const;

	private:
		struct PackedKA; // Key::ID + Asset::ID
		bool IsValid2(Height hScheme, ECC::Point::Native& comm, const ECC::Point::Native* pGen) const;
	};

	inline bool operator < (const Output::Ptr& a, const Output::Ptr& b) { return *a < *b; }

	struct ShieldedTxo
	{
		static void UpdateState(ECC::Hash::Value&, const ECC::Point::Storage&);

		struct Ticket
		{
			ECC::Point m_SerialPub; // blinded
			ECC::SignatureGeneralized<2> m_Signature;

			bool IsValid(ECC::Point::Native&) const;
			void get_Hash(ECC::Hash::Value&) const;
		};

		struct DescriptionBase
		{
			Height m_Height;
		};

		struct DescriptionOutp
			:public DescriptionBase
		{
			TxoID m_ID;
			ECC::Point m_SerialPub; // blinded
			ECC::Point m_Commitment;

			void get_Hash(Merkle::Hash&) const;
		};

		struct DescriptionInp
			:public DescriptionBase
		{
			ECC::Point m_SpendPk;

			void get_Hash(Merkle::Hash&) const;
		};

		ECC::Point m_Commitment;
		ECC::RangeProof::Confidential m_RangeProof;
		Asset::Proof::Ptr m_pAsset;
		Ticket m_Ticket;

		void Prepare(ECC::Oracle&, Height hScheme) const;
		bool IsValid(ECC::Oracle&, Height hScheme, ECC::Point::Native& comm, ECC::Point::Native& ser) const;

		ShieldedTxo& operator = (const ShieldedTxo&); // clone

		struct PublicGen;
		struct Viewer;
		struct Data;
		struct DataParams; // just a fwd-declaration of Data::Params

		struct BaseKey
		{
			Key::Index m_nIdx;
			bool m_IsCreatedByViewer;
			ECC::Scalar m_kSerG;

			bool operator ==(const BaseKey& other) const
			{
				return
					m_kSerG == other.m_kSerG &&
					m_nIdx == other.m_nIdx &&
					m_IsCreatedByViewer == other.m_IsCreatedByViewer;
			}
			
			bool operator!=(const BaseKey& other) const
			{
				return !operator==(other);
			}

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_nIdx
					& m_IsCreatedByViewer
					& m_kSerG;
			}
		};

		struct User
		{
			PeerID m_Sender;
			ECC::uintBig m_pMessage[2];
			bool operator == (const User& other) const
			{
				return
					m_Sender == other.m_Sender &&
					m_pMessage[0] == other.m_pMessage[0] &&
					m_pMessage[1] == other.m_pMessage[1];
			}

			bool operator != (const User& other) const
			{
				return !operator == (other);
			}

#pragma pack (push, 1)
			struct PackedMessage
			{
				typedef uintBig_t<16> TxID;
				TxID m_TxID;
				// value from 1 to 64 to set privacy level for transaction.
				// if value less than 64 - allow extract coin from pool before anonymity set was reached
				uint8_t m_MaxPrivacyMinAnonymitySet;
				uint64_t m_ReceiverOwnID;
				uint8_t m_Flags;
				uint8_t m_Padding[sizeof(m_pMessage)
									- sizeof(TxID)
									- sizeof(uint8_t)
									- sizeof(uint64_t)
									- sizeof(uint8_t)];
			};
#pragma pack (pop)
			static PackedMessage* ToPackedMessage(User& user)
			{
				return reinterpret_cast<PackedMessage*>(user.m_pMessage);
			}

			static const PackedMessage* ToPackedMessage(const User& user)
			{
				return reinterpret_cast<const PackedMessage*>(user.m_pMessage);
			}

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_Sender
					& m_pMessage;
			}
		};

		struct ID
		{
			BaseKey m_Key;
			User m_User;
			Amount m_Value;
			Asset::ID m_AssetID = 0;
			bool operator == (const ID&) const;
			bool operator != (const ID& other) const
			{
				return !operator==(other);
			}

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_Value
					& m_AssetID
					& m_Key
					& m_User;
			}

			void get_SkOut(ECC::Scalar::Native&, Amount fee, Key::IKdf& kdf) const;
			void get_SkOutPreimage(ECC::Hash::Value&, Amount fee) const;

			static const Key::Index s_iChildOut = static_cast<uint32_t>(-2);
		};

		struct Voucher
		{
			// single-usage
			Ticket m_Ticket;
			ECC::Hash::Value m_SharedSecret;
			ECC::Signature m_Signature;

			void get_Hash(ECC::Hash::Value&) const;
			bool IsValid(const PeerID&) const;
			bool IsValid(const ECC::Point::Native&) const;
		};
	};

#define BeamKernelsAll(macro) \
	macro(1, Std) \
	macro(2, AssetEmit) \
	macro(3, ShieldedOutput) \
	macro(4, ShieldedInput) \
	macro(5, AssetCreate) \
	macro(6, AssetDestroy) \
	macro(7, ContractCreate) \
	macro(8, ContractInvoke) \

#define THE_MACRO(id, name) struct TxKernel##name;
	BeamKernelsAll(THE_MACRO)
#undef THE_MACRO

	struct TxKernel
	{
		typedef std::unique_ptr<TxKernel> Ptr;

		struct Subtype
		{
			enum Enum {
#define THE_MACRO(id, name) name = id,
				BeamKernelsAll(THE_MACRO)
#undef THE_MACRO
			};
		};

		Amount			m_Fee;			// can be 0 (for instance for coinbase transactions)
		HeightRange		m_Height;
		bool			m_CanEmbed;

		struct Internal
		{
			bool m_HasNonStd = false; // is it or does it contain non-std kernels with side effects and mutual dependencies. Those should not be sorted!
			Merkle::Hash m_ID; // unique kernel identifier in the system.
		} m_Internal;

		TxKernel()
			:m_Fee(0)
			,m_CanEmbed(false)
		{}

		std::vector<Ptr> m_vNested; // nested kernels, included in the signature.

		static const uint32_t s_MaxRecursionDepth = 2;

		static void TestRecursion(uint32_t n)
		{
			if (n > s_MaxRecursionDepth)
				throw std::runtime_error("recursion too deep");
		}

		virtual ~TxKernel() {}
		virtual Subtype::Enum get_Subtype() const = 0;
		virtual void UpdateID() = 0;
		virtual void TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent = nullptr) const = 0;
		virtual void AddStats(TxStats&) const; // including self and nested
		virtual int cmp_Subtype(const TxKernel&) const;
		virtual void Clone(Ptr&) const = 0;

		bool IsValid(Height hScheme, std::string* psErr = nullptr) const;

		HeightRange get_EffectiveHeightRange() const;

		struct LongProof; // legacy

		int cmp(const TxKernel&) const;
		COMPARISON_VIA_CMP


		struct IWalker
		{
			uint32_t m_nKrnIdx = 0;

			virtual bool OnKrn(const TxKernel&) = 0;

			bool Process(const std::vector<TxKernel::Ptr>&);
			bool Process(const TxKernel&);
		};

		struct Checkpoint :public Exc::Checkpoint {

			const TxKernel& m_Krn;
			Checkpoint(const TxKernel& krn) :m_Krn(krn) {}

			void Dump(std::ostream& os) override {
				os << "Kernel ID=" << m_Krn.m_Internal.m_ID << ", type=" << (uint32_t) m_Krn.get_Subtype();
			}
		};

#define THE_MACRO(id, name) \
		TxKernel##name & CastTo_##name() { \
			assert(get_Subtype() == Subtype::name); \
			return Cast::Up<TxKernel##name>(*this); \
		} \
		const TxKernel##name & CastTo_##name() const { \
			return Cast::NotConst(*this).CastTo_##name(); \
		}

		BeamKernelsAll(THE_MACRO)
#undef THE_MACRO

	protected:
		void HashBase(ECC::Hash::Processor&) const;
		void HashNested(ECC::Hash::Processor&) const;
		void CopyFrom(const TxKernel&);
		void TestValidBase(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent, ECC::Point::Native* pComm = nullptr) const;
	private:
		void operator = (const TxKernel&);
	};

	struct TxKernelStd
		:public TxKernel
	{
		typedef std::unique_ptr<TxKernelStd> Ptr;

		ECC::Point		m_Commitment;	// aggregated, including nested kernels
		ECC::Signature	m_Signature;	// For the whole body, including nested kernels

		struct HashLock
		{
			ECC::Hash::Value m_Value;
			bool m_IsImage = false; // not serialized. Used only internally to get the kernel ID after it'd be substituted

			const ECC::Hash::Value& get_Image(ECC::Hash::Value& hv) const;

			int cmp(const HashLock&) const;
			COMPARISON_VIA_CMP
		};

		std::unique_ptr<HashLock> m_pHashLock;

		struct RelativeLock
		{
			Merkle::Hash m_ID;
			Height m_LockHeight;

			int cmp(const RelativeLock&) const;
			COMPARISON_VIA_CMP
		};

		std::unique_ptr<RelativeLock> m_pRelativeLock;

		virtual ~TxKernelStd() {}
		virtual Subtype::Enum get_Subtype() const override;
		virtual void UpdateID() override;
		virtual void TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent = nullptr) const override;
		virtual int cmp_Subtype(const TxKernel&) const override;
		virtual void Clone(TxKernel::Ptr&) const override;

		void Sign(const ECC::Scalar::Native&); // suitable for aux kernels, created by single party
	};

	struct TxKernelNonStd
		:public TxKernel
	{
		Merkle::Hash m_Msg; // message to sign, diffetent from ID

		virtual void UpdateID() override;
		void UpdateMsg();
		void MsgToID();

	protected:
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const = 0;
		virtual void HashSelfForID(ECC::Hash::Processor&) const = 0;
		void CopyFrom(const TxKernelNonStd&);
		virtual void AddStats(TxStats&) const override;
	};

	struct TxKernelAssetControl
		:public TxKernelNonStd
	{
		PeerID m_Owner;

		ECC::Point m_Commitment;	// aggregated, including nested kernels
		ECC::SignatureGeneralized<1> m_Signature;

		void Sign_(const ECC::Scalar::Native& sk, const ECC::Scalar::Native& skAsset);
		void Sign(const ECC::Scalar::Native& sk, Key::IKdf&, const Asset::Metadata&);

		void get_Sk(ECC::Scalar::Native&, Key::IKdf&); // pseudo-random sk for this kernel

	protected:
		void CopyFrom(const TxKernelAssetControl&);
		void TestValidAssetCtl(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent) const;
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const override;
		virtual void HashSelfForID(ECC::Hash::Processor&) const override;
	};

	struct TxKernelAssetEmit
		:public TxKernelAssetControl
	{
		typedef std::unique_ptr<TxKernelAssetEmit> Ptr;

		Asset::ID m_AssetID;
		AmountSigned m_Value;
		TxKernelAssetEmit() : m_AssetID(Asset::s_InvalidID), m_Value(0) {}

		virtual ~TxKernelAssetEmit() {}
		virtual Subtype::Enum get_Subtype() const override;
		virtual void TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent = nullptr) const override;
		virtual void Clone(TxKernel::Ptr&) const override;
	protected:
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const override;
	};

	struct TxKernelAssetCreate
		:public TxKernelAssetControl
	{
		typedef std::unique_ptr<TxKernelAssetCreate> Ptr;

		Asset::Metadata m_MetaData;

		void Sign(const ECC::Scalar::Native& sk, Key::IKdf&);

		virtual ~TxKernelAssetCreate() {}
		virtual Subtype::Enum get_Subtype() const override;
		virtual void TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent = nullptr) const override;
		virtual void Clone(TxKernel::Ptr&) const override;
	protected:
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const override;
	};

	struct TxKernelAssetDestroy
		:public TxKernelAssetControl
	{
		typedef std::unique_ptr<TxKernelAssetDestroy> Ptr;

		Asset::ID m_AssetID;
		Amount m_Deposit = 0;
        TxKernelAssetDestroy(): m_AssetID(Asset::s_InvalidID) {}

		bool IsCustomDeposit() const;
		Amount get_Deposit() const;

		virtual ~TxKernelAssetDestroy() {}
		virtual Subtype::Enum get_Subtype() const override;
		virtual void TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent = nullptr) const override;
		virtual void Clone(TxKernel::Ptr&) const override;
	protected:
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const override;
	};

	struct TxKernelShieldedOutput
		:public TxKernelNonStd
	{
		typedef std::unique_ptr<TxKernelShieldedOutput> Ptr;

		ShieldedTxo m_Txo;

		virtual ~TxKernelShieldedOutput() {}
		virtual Subtype::Enum get_Subtype() const override;
		virtual void TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent = nullptr) const override;
		virtual void AddStats(TxStats&) const override;
		virtual void Clone(TxKernel::Ptr&) const override;
	protected:
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const override;
		virtual void HashSelfForID(ECC::Hash::Processor&) const override;
	};

	struct TxKernelShieldedInput
		:public TxKernelNonStd
	{
		typedef std::unique_ptr<TxKernelShieldedInput> Ptr;

		TxoID m_WindowEnd; // ID of the 1st element outside the window
		Lelantus::Proof m_SpendProof;
		Asset::Proof::Ptr m_pAsset;

		struct NotSerialized {
			ECC::Hash::Value m_hvShieldedState;
		} m_NotSerialized;

		void Sign(Lelantus::Prover&, Asset::ID aids);

		virtual ~TxKernelShieldedInput() {}
		virtual Subtype::Enum get_Subtype() const override;
		virtual void TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent = nullptr) const override;
		virtual void AddStats(TxStats&) const override;
		virtual void Clone(TxKernel::Ptr&) const override;
	protected:
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const override;
		virtual void HashSelfForID(ECC::Hash::Processor&) const override;
	};

	struct TxKernelContractControl
		:public TxKernelNonStd
	{
		ECC::Point m_Commitment; // arbitrary blinding factor + all the values consumed/emitted by the contract
		ECC::Signature m_Signature; // aggreagtedmulti-signature of the blinding factor + all the keys required by the contract

		ByteBuffer m_Args;
		bool m_Dependent = false;

		virtual void TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent = nullptr) const override;
		virtual void AddStats(TxStats&) const override;

		void Prepare(ECC::Hash::Processor&, const Merkle::Hash* pParentCtx) const;
		void Sign(const ECC::Scalar::Native*, uint32_t nKeys, const ECC::Point::Native& ptFunds, const Merkle::Hash* pParentCtx);

	protected:
		void CopyFrom(const TxKernelContractControl&);
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const override;
		virtual void HashSelfForID(ECC::Hash::Processor&) const override;
	};

	struct TxKernelContractCreate
		:public TxKernelContractControl
	{
		ByteBuffer m_Data;

		typedef std::unique_ptr<TxKernelContractCreate> Ptr;

		virtual ~TxKernelContractCreate() {}
		virtual Subtype::Enum get_Subtype() const override;
		virtual void Clone(TxKernel::Ptr&) const override;
		virtual void AddStats(TxStats&) const override;
	protected:
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const override;
	};

	struct TxKernelContractInvoke
		:public TxKernelContractControl
	{
		ContractID m_Cid;
		uint32_t m_iMethod;

		typedef std::unique_ptr<TxKernelContractInvoke> Ptr;

		virtual ~TxKernelContractInvoke() {}
		virtual Subtype::Enum get_Subtype() const override;
		virtual void Clone(TxKernel::Ptr&) const override;
	protected:
		virtual void HashSelfForMsg(ECC::Hash::Processor&) const override;
	};

	inline bool operator < (const TxKernel::Ptr& a, const TxKernel::Ptr& b) { return *a < *b; }

	struct DependentContext
	{
		static void get_Ancestor(Merkle::Hash& hvRes, const Merkle::Hash& hvParent, const Merkle::Hash& hvTx)
		{
			ECC::Hash::Processor()
				<< "dep.tx"
				<< hvParent
				<< hvTx
				>> hvRes;
		}
	};

	struct TxBase
	{
		class Context;
		static int CmpInOut(const Input&, const Output&);

		struct IReader
		{
			typedef std::unique_ptr<IReader> Ptr;

			// during iterations those pointers are guaranteed to be valid during at least 1 consequent iteration
			const Input* m_pUtxoIn;
			const Output* m_pUtxoOut;
			const TxKernel* m_pKernel;

			virtual ~IReader() {}
			virtual void Clone(Ptr&) = 0;
			virtual void Reset() = 0;
			// For all the following methods: the returned pointer should be valid during at least 2 consequent calls!
			virtual void NextUtxoIn() = 0;
			virtual void NextUtxoOut() = 0;
			virtual void NextKernel() = 0;

			void Compare(IReader&& rOther, bool& bICover, bool& bOtherCovers);
			size_t get_SizeNetto(); // account only for elements. Ignore offset and array sizes
			void AddStats(TxStats&);
		};

		struct IWriter
		{
			virtual ~IWriter() {}
			virtual void Write(const Input&) = 0;
			virtual void Write(const Output&) = 0;
			virtual void Write(const TxKernel&) = 0;

			void Dump(IReader&&);
			bool Combine(IReader** ppR, int nR, const volatile bool& bStop); // combine consequent blocks, merge-sort and delete consumed outputs
			// returns false if aborted
			bool Combine(IReader&& r0, IReader&& r1, const volatile bool& bStop);
		};

		static void Fail_Order();
		static void Fail_Signature();

		ECC::Scalar m_Offset;
	};

	struct TxVectors
	{
		struct Perishable
		{
			std::vector<Input::Ptr> m_vInputs;
			std::vector<Output::Ptr> m_vOutputs;
			size_t NormalizeP(); // w.r.t. the standard, delete spent outputs. Returns the num deleted
		};

		struct Eternal
		{
			std::vector<TxKernel::Ptr> m_vKernels;
			void NormalizeE();
		};

		class Reader :public TxBase::IReader {
			size_t m_pIdx[3];
		public:
			const Perishable& m_P;
			const Eternal& m_E;
			Reader(const Perishable& p, const Eternal& e) :m_P(p) ,m_E(e) {}
			// IReader
			virtual void Clone(Ptr&) override;
			virtual void Reset() override;
			virtual void NextUtxoIn() override;
			virtual void NextUtxoOut() override;
			virtual void NextKernel() override;
		};

		struct Writer :public TxBase::IWriter
		{
			Perishable& m_P;
			Eternal& m_E;
			Writer(Perishable& p, Eternal& e) :m_P(p), m_E(e) {}

			virtual void Write(const Input&) override;
			virtual void Write(const Output&) override;
			virtual void Write(const TxKernel&) override;
		};

		struct Full
			:public TxVectors::Perishable
			,public TxVectors::Eternal
		{
			Reader get_Reader() const {
				return Reader(*this, *this);
			}

			size_t Normalize();

			void MoveInto(Full& trg);
		};
	};

	struct Transaction
		:public TxBase
		,public TxVectors::Full
	{
		typedef std::shared_ptr<Transaction> Ptr;

		void TestValid(Context&) const; // Explicit fees are considered "lost" in the transactions (i.e. would be collected by the miner)
		bool IsValid(Context&, std::string* psErr = nullptr) const;

		typedef uintBig_t<ECC::nBytes> KeyType; // key len for map of transactions. Can actually be less than 256 bits.

		void get_Key(KeyType&) const;

		struct FeeSettings
		{
			Amount m_Output;
			Amount m_Kernel; // nested kernels are accounted too
			Amount m_ShieldedInputTotal; // including 1 kernel price
			Amount m_ShieldedOutputTotal; // including 1 kernel price
			Amount m_Default; // for std tx

			struct Bvm {
				Amount m_ChargeUnitPrice;
				Amount m_Minimum;
				Amount m_ExtraBytePrice;
				uint32_t m_ExtraSizeFree;
			} m_Bvm;

			static const FeeSettings& get(Height);

			Amount Calculate(const Transaction&) const;
			Amount Calculate(const TxStats&) const;
			Amount CalculateForBvm(const TxStats&, uint32_t nBvmCharge) const;

			Amount get_DefaultStd() const;
			Amount get_DefaultShieldedOut(uint32_t nNumShieldedOutputs = 1) const;
		};
	};

	struct Block
	{
		// Different parts of the block are split into different structs, so that they can be manipulated (transferred, processed, saved and etc.) independently
		// For instance, there is no need to keep PoW (at least in SPV client) once it has been validated.

		struct PoW
		{
			// equihash parameters. 
			// Parameters recommended by BTG are 144/5, to make it asic-resistant (~1GB average, spikes about 1.5GB). On CPU solve time about 1 minutes
			// The following are the parameters for testnet, to make it of similar size, and much faster solve time, to test concurrency and difficulty adjustment
			static const uint32_t N = 150;
			static const uint32_t K = 5;

			static const uint32_t nNumIndices		= 1 << K; // 32
			static const uint32_t nBitsPerIndex		= N / (K + 1) + 1; // 26

			static const uint32_t nSolutionBits		= nNumIndices * nBitsPerIndex; // 832 bits

			static_assert(!(nSolutionBits & 7), "PoW solution should be byte-aligned");
			static const uint32_t nSolutionBytes	= nSolutionBits >> 3; // 104 bytes

			std::array<uint8_t, nSolutionBytes>	m_Indices;

			typedef uintBig_t<8> NonceType;
			NonceType m_Nonce; // 8 bytes. The overall solution size is 96 bytes.
			Difficulty m_Difficulty;

			bool IsValid(const void* pInput, uint32_t nSizeInput, Height) const;

			using Cancel = std::function<bool(bool bRetrying)>;
			// Difficulty and Nonce must be initialized. During the solution it's incremented each time by 1.
			// returns false only if cancelled
			bool Solve(const void* pInput, uint32_t nSizeInput, Height, const Cancel& = [](bool) { return false; });

		private:
			struct Helper;
		};

		static void get_HashContractVar(Merkle::Hash&, const Blob& key, const Blob& val);
		static void get_HashContractLog(Merkle::Hash&, const Blob& key, const Blob& val, uint32_t nPos);

		struct SystemState
		{
			typedef HeightHash ID;

			struct Evaluator
				:public Merkle::IEvaluator
			{
				Height m_Height;

				// The state Definition is defined as Hash[ History | Live ]
				// Before Fork2: Live = Utxos
				// Before Fork3: Live = Hash[ Utxos | Hash[Shielded | Assets] ]
				// Past Fork3:
				//		CSA = Hash[ Contracts | Hash[Shielded | Assets] ]
				//		KL = Hash[ Kernels | Logs ]
				//		Live = Hash[ KL | CSA ]

				bool get_Definition(Merkle::Hash&);
				void GenerateProof(); // same as above, except it's used for proof generation, and the resulting hash is not evaluated

				virtual bool get_History(Merkle::Hash&);
				virtual bool get_Live(Merkle::Hash&);
				virtual bool get_CSA(Merkle::Hash&);
				virtual bool get_KL(Merkle::Hash&);
				virtual bool get_Utxos(Merkle::Hash&);
				virtual bool get_Kernels(Merkle::Hash&);
				virtual bool get_Logs(Merkle::Hash&);
				virtual bool get_Shielded(Merkle::Hash&);
				virtual bool get_Assets(Merkle::Hash&);
				virtual bool get_Contracts(Merkle::Hash&);

				bool get_SA(Merkle::Hash&);
			};

			struct Sequence
			{
				struct Prefix {
					Height				m_Height;
					Merkle::Hash		m_Prev;			// explicit referebce to prev
					Difficulty::Raw		m_ChainWork;
				};

				struct Element
				{
					Merkle::Hash	m_Kernels; // Before Fork3: kernels (of this block only), after Fork3: Utxos
					Merkle::Hash	m_Definition;
					Timestamp		m_TimeStamp;
					PoW				m_PoW;
				};
			};

			struct Full
				:public Sequence::Prefix
				,public Sequence::Element
			{
				void NextPrefix();

				void get_HashForPoW(Merkle::Hash&) const; // all except PoW
				void get_Hash(Merkle::Hash&) const; // all

				void get_ID(ID&) const;

				bool IsSane() const;
				bool IsValidPoW() const;
				bool IsValid() const {
					return IsSane() && IsValidPoW(); 
				}
                bool GeneratePoW(const PoW::Cancel& = [](bool) { return false; });

				// the most robust proof verification - verifies the whole proof structure
				bool IsValidProofState(const ID&, const Merkle::HardProof&) const;

				bool IsValidProofKernel(const TxKernel&, const TxKernel::LongProof&) const;
				bool IsValidProofKernel(const Merkle::Hash& hvID, const TxKernel::LongProof&) const;
				bool IsValidProofKernel(const Merkle::Hash& hvID, const Merkle::Proof&) const;

				bool IsValidProofLog(const Merkle::Hash& hvLog, const Merkle::Proof&) const;

				bool IsValidProofUtxo(const ECC::Point&, const Input::Proof&) const;
				bool IsValidProofShieldedOutp(const ShieldedTxo::DescriptionOutp&, const Merkle::Proof&) const;
				bool IsValidProofShieldedInp(const ShieldedTxo::DescriptionInp&, const Merkle::Proof&) const;
				bool IsValidProofAsset(const Asset::Full&, const Merkle::Proof&) const;
				bool IsValidProofContract(const Blob& key, const Blob& val, const Merkle::Proof&) const;

				int cmp(const Full&) const;
				COMPARISON_VIA_CMP

				bool IsNext(const Full& sNext) const;

			private:
				void get_HashInternal(Merkle::Hash&, bool bTotal) const;
				bool IsValidProofShielded(Merkle::Hash&, const Merkle::Proof&) const;

				struct ProofVerifier;
				struct ProofVerifierHard;
			};

			struct IHistory
			{
				// should provide access to some recent states of the active branch

				struct IWalker {
					virtual bool OnState(const Block::SystemState::Full&) = 0;
				};

				virtual bool Enum(IWalker&, const Height* pBelow) = 0;
				virtual bool get_At(Full&, Height) = 0;
				virtual void AddStates(const Full*, size_t nCount) = 0;
				virtual void DeleteFrom(Height) = 0;

				bool get_Tip(Full&); // zero-inits if no tip
			};

			struct HistoryMap
				:public IHistory
			{
				// simple impl
				std::map<Height, Full> m_Map;
				void ShrinkToWindow(Height dh);

				virtual bool Enum(IWalker&, const Height* pBelow) override;
				virtual bool get_At(Full&, Height) override;
				virtual void AddStates(const Full*, size_t nCount) override;
				virtual void DeleteFrom(Height) override;
			};

		};

		struct BodyBase
			:public TxBase
		{
			void ZeroInit();

			struct IMacroReader
				:public IReader
			{
				virtual void get_Start(BodyBase&, SystemState::Sequence::Prefix&) = 0;
				virtual bool get_NextHdr(SystemState::Sequence::Element&) = 0;
			};

			struct IMacroWriter
				:public IWriter
			{
				virtual void put_Start(const BodyBase&, const SystemState::Sequence::Prefix&) = 0;
				virtual void put_NextHdr(const SystemState::Sequence::Element&) = 0;

				bool CombineHdr(IMacroReader&& r0, IMacroReader&& r1, const volatile bool& bStop);
			};

			void Merge(const BodyBase& next);
		};

		struct Body
			:public BodyBase
			,public TxVectors::Full
		{
		};

		struct ChainWorkProof;

		struct Builder
		{
			ECC::Scalar::Native m_Offset; // the sign is opposite
			TxVectors::Full m_Txv;

			Key::Index m_SubIdx;
			Key::IKdf& m_Coin;
			Key::IPKdf& m_Tag;
			Height m_Height;

			Builder(Key::Index, Key::IKdf& coin, Key::IPKdf& tag, Height);

			void AddCoinbaseAndKrn();
			void AddCoinbaseAndKrn(Output::Ptr&, TxKernel::Ptr&);
			void AddFees(Amount fees);
			void AddFees(Amount fees, Output::Ptr&);
		};
	};

	struct TxKernel::LongProof
	{
		Merkle::Proof m_Inner;
		Block::SystemState::Full m_State;
		Merkle::HardProof m_Outer;

		bool empty() const { return !m_State.m_Height; }

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_Inner
				& m_State
				& m_Outer;
		}
	};

	class TxBase::Context
	{
		bool ShouldVerify(uint32_t& iV) const;
		void TestAbort() const;
		void TestHeightNotEmpty() const;
		void HandleElementHeightStrict(const HeightRange&);

	public:
		// Tests the validity of all the components, overall arithmetics, and the lexicographical order of the components.
		// Determines the min/max block height that the transaction can fit, wrt component heights and maturity policies
		// Does *not* check the existence of the input UTXOs
		//
		// Validation formula
		//
		// Sum(Input UTXOs) = Sum(Output UTXOs) + Sum(Output Kernels.Excess) + m_Offset*G [ + Sum(Fee)*H ]
		//
		// For transaction validation fees are considered as implicit outputs (i.e. Sum(Fee)*H should be added for the right equation side)
		//
		// For a block validation Fees are not accounted for, since they are consumed by new outputs injected by the miner.
		// However Each block contains extra outputs (coinbase) for block closure, which should be subtracted from the outputs for sum validation.
		//
		// Define: Sigma = Sum(Output UTXOs) - Sum(Input UTXOs) + Sum(Output Kernels.Excess) + m_Offset*G
		// In other words Sigma = <all outputs> - <all inputs>
		// Sigma is either zero or -Sum(Fee)*H, depending on what we validate

		struct Params
		{
			bool m_bAllowUnsignedOutputs; // allow outputs without signature (commitment only). Applicable for cut-through blocks only, outputs that are supposed to be consumed in the later block.

			// for multi-tasking, parallel verification
			uint32_t m_nVerifiers;
			volatile bool* m_pAbort;

			Params(); // defaults
		};

		Params m_Params;

		ECC::Point::Native m_Sigma;
		TxStats m_Stats;
		HeightRange m_Height;

		uint32_t m_iVerifier;

		Context()
		{
			Reset();
		}

		void Reset();

		void ValidateAndSummarizeStrict(const TxBase&, IReader&&);
		bool ValidateAndSummarize(const TxBase&, IReader&&, std::string* psErr = nullptr);
		void MergeStrict(const Context&);

		// hi-level functions, should be used after all parts were validated and merged
		void TestValidTransaction();
		void TestSigma();
		void TestValidBlock();
	};

	struct Block::ChainWorkProof
	{
		// Compressed consecutive states (likely to appear at the end)
		struct Heading {
			SystemState::Sequence::Prefix m_Prefix;
			std::vector<SystemState::Sequence::Element> m_vElements;
		} m_Heading;
		// other states
		std::vector<SystemState::Full> m_vArbitraryStates;
		// compressed proof
		Merkle::MultiProof m_Proof;
		// last node to go from History to Definition.
		Merkle::Hash m_hvRootLive;
		// crop thereshold. Off by default
		Difficulty::Raw m_LowerBound;

		struct ISource
		{
			virtual void get_StateAt(SystemState::Full&, const Difficulty::Raw&) = 0;
			virtual void get_Proof(Merkle::IProofBuilder&, Height) = 0;
		};

		ChainWorkProof()
		{
			ZeroInit();
		}

		void Reset();
		void Create(ISource&, const SystemState::Full& sRoot);
		bool IsValid(SystemState::Full* pTip = NULL) const;
		bool Crop(); // according to current bound
		bool Crop(const ChainWorkProof& src);
		bool IsEmpty() const { return m_Heading.m_vElements.empty(); }

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_Heading.m_Prefix
				& m_Heading.m_vElements
				& m_vArbitraryStates
				& m_Proof
				& m_hvRootLive
				& m_LowerBound;
		}

		struct IStateWalker
		{
			virtual bool OnState(const SystemState::Full&, bool bIsTip) = 0;
		};

		// enumerates all the embedded states in standard order (from lo to hi)
		bool EnumStates(IStateWalker&) const;
		void UnpackStates(std::vector<SystemState::Full>&) const;

	private:
		struct Sampler;
		bool IsValidInternal(size_t& iState, size_t& iHash, const Difficulty::Raw& lowerBound, SystemState::Full* pTip) const;
		void ZeroInit();
		bool EnumStatesHeadingOnly(IStateWalker&) const; // skip arbitrary
	};

	struct FundsChangeMap
	{
		std::map<Asset::ID, AmountBig::Type> m_Map;

		void Add(Amount val, Asset::ID, bool bSpend);
		void Add(const AmountBig::Type&, Asset::ID);
		void ToCommitment(ECC::Point::Native&) const;
	};
}

inline ECC::Hash::Processor& operator << (ECC::Hash::Processor& hp, const beam::PeerID& pid) {
	return hp << Cast::Down<ECC::Hash::Value>(pid);
}

// TODO: review this types, they don't have standard layout
inline void ZeroObject(beam::CoinID& x)
{
	ZeroObjectUnchecked(x);
}

inline void ZeroObject(beam::Block::SystemState::Full& x)
{
	ZeroObjectUnchecked(x);
}

inline void ZeroObject(beam::Asset::Full& x)
{
	ZeroObjectUnchecked(x);
}