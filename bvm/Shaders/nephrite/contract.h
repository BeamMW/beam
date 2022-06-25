#pragma once
#include "pool.h"
#include "../upgradable3/contract.h"

namespace Nephrite
{
    static const ShaderID s_pSID[] = {
        { 0x51,0x35,0x1a,0x94,0x4c,0xc9,0xb9,0xe5,0xf8,0xd7,0x0f,0x5d,0xeb,0xb2,0xf6,0x45,0x0d,0x81,0xc2,0x91,0x8d,0xef,0x25,0x85,0x4f,0x97,0x10,0xed,0x16,0x78,0x95,0x90 }
    };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_State = 0;
        static const uint8_t s_Epoch_Stable = 3;
        static const uint8_t s_Balance = 4;
        static const uint8_t s_StabPool = 5;
        static const uint8_t s_Trove = 7;
    };

    typedef MultiPrecision::Float Float;

    template <typename T> struct Pair_T
    {
        T Tok;
        T Col;
    };

    struct Pair :public Pair_T<Amount>
    {
        Float get_Rcr() const
        {
            // rcr == raw collateralization ratio, i.e. Col/Tok
            // Don't care about Tok==0 case (our Float doesn't handle inf), valid troves must always have Tok no lesser than liquidation reserve
            return Float(Col) / Float(Tok);
        }

        int CmpRcr(const Pair& x) const
        {
            // somewhat faster than calculating rcrs individually
            auto a = MultiPrecision::From(Col) * MultiPrecision::From(x.Tok);
            auto b = MultiPrecision::From(Tok) * MultiPrecision::From(x.Col);
            return a.cmp(b);
        }
    };

    struct Flow
    {
        Amount m_Val;
        uint8_t m_Spend; // not necessarily normalized, i.e. can be zero or any nnz value when comes from user

        void operator += (const Flow& c) {
            Add(c.m_Val, c.m_Spend);
        }

        void operator -= (const Flow& c) {
            Add(c.m_Val, !c.m_Spend);
        }

        void Add(Amount x, uint8_t bSpend)
        {
            if ((!m_Spend) == (!bSpend))
                Strict::Add(m_Val, x);
            else
            {
                if (m_Val >= x)
                    m_Val -= x;
                else
                {
                    m_Val = x - m_Val;
                    m_Spend = !m_Spend;
                }
            }
        }
    };

    typedef Pair_T<Flow> FlowPair;

    typedef HomogenousPool::MultiEpoch<2> ExchangePool;
    typedef HomogenousPool::SingleEpoch<1> RedistPoolBase;

    struct Balance
    {
        struct Key {
            uint8_t m_Tag = Tags::s_Balance;
            PubKey m_Pk;
        };

        Pair m_Amounts;
        Amount m_Gov;
    };

    struct EpochKey {
        uint8_t m_Tag;
        uint32_t m_iEpoch;
    };

    struct StabPoolEntry
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_StabPool;
            PubKey m_pkUser;
        };

        ExchangePool::User m_User;
        Height m_hLastModify;
    };

    struct Trove
    {
        typedef uint32_t ID;

        struct Key
        {
            uint8_t m_Tag = Tags::s_Trove;
            ID m_iTrove;
        };

        PubKey m_pkOwner;
        Pair m_Amounts;
        RedistPoolBase::User m_RedistUser; // accumulates enforced liquidations
        ID m_iNext;
    };

    struct Settings
    {
        ContractID m_cidDaoVault;
        ContractID m_cidOracle1;
        ContractID m_cidOracle2;
        Amount m_TroveLiquidationReserve;
        AssetID m_AidGov;
        Height m_hMinRedemptionHeight;
    };

    struct Global
    {
        Settings m_Settings;
        AssetID m_Aid;

        struct Troves
        {
            Trove::ID m_iLastCreated;
            Trove::ID m_iHead;
            Pair m_Totals; // Total debt (== minted tokens) and collateral in all troves

        } m_Troves;

        struct RedistPool
            :public RedistPoolBase
        {
            void Add(Trove& t)
            {
                UserAdd(t.m_RedistUser, t.m_Amounts.Tok);
            }

            bool IsUnchanged(const Trove& t) const
            {
                return m_Active.IsUnchanged(t.m_RedistUser);
            }

            void Remove(Trove& t)
            {
                // try to avoid recalculations if nothing changed, to prevent inaccuracies
                //
                // // should not overflow, all values are bounded by totals.
                if (IsUnchanged(t))
                {
                    m_Active.m_Sell -= t.m_Amounts.Tok; // silent removal
                    m_Active.m_Users--;
                }
                else
                {
                    User::Out out;
                    UserDel(t.m_RedistUser, out);
                    UpdAmountsPostRemove(t.m_Amounts, out);
                }
            }

            Pair get_UpdatedAmounts(const Trove& t) const
            {
                auto ret = t.m_Amounts;
                if (!IsUnchanged(t))
                {
                    User::Out out;
                    t.m_RedistUser.DelRO_(m_Active, out);
                    UpdAmountsPostRemove(ret, out);
                }
                return ret;
            }

            bool Liquidate(Trove& t)
            {
                if (!get_TotalSell())
                    return false; // empty

                Trade<Mode::Grow, 0>(t.m_Amounts.Tok, t.m_Amounts.Col);

                return true;
            }

        private:
            static void UpdAmountsPostRemove(Pair& vals, const User::Out& out)
            {
                vals.Tok = out.m_Sell;
                vals.Col += out.m_pBuy[0];
            }

        } m_RedistPool;

        struct StabilityPool
            :public ExchangePool
        {
            struct Reward
            {
                Height m_hLast;
                Height m_hEnd;
                Amount m_Remaining;
            } m_Reward;

            bool AddReward(Height h)
            {
                if (!m_Reward.m_Remaining || (h <= m_Reward.m_hLast) || !get_TotalSell())
                    return false;

                Amount valAdd = m_Reward.m_Remaining;
                if (h < m_Reward.m_hEnd)
                {
                    auto k = Float(h - m_Reward.m_hLast) / Float(m_Reward.m_hEnd - m_Reward.m_hLast);
                    Amount val = HomogenousPool::Round(Float(valAdd) * k);
                    valAdd = std::min(valAdd, val);
                }

                Trade<Mode::Neutral, 1>(0, valAdd);
                return true;
            }

            bool LiquidatePartial(Trove& t)
            {
                Amount valS = get_TotalSell();
                if (!valS)
                    return false;

                Amount valB;

                if (valS >= t.m_Amounts.Tok)
                {
                    valS = t.m_Amounts.Tok;
                    valB = t.m_Amounts.Col;
                    _POD_(t.m_Amounts).SetZero();
                }
                else
                {
                    valB = t.m_Amounts.get_Rcr() * Float(valS);
                    assert(valB <= t.m_Amounts.Col);
                    valB = std::min(valB, t.m_Amounts.Col); // for more safety, but should be ok

                    t.m_Amounts.Tok -= valS;
                    t.m_Amounts.Col -= valB; 
                }

                Trade<Mode::Burn, 0>(valS, valB);
                return true;
            }

        } m_StabPool;

        struct Price
        {
            Float m_Value; // 1 col == 1 tok * m_Value
            Float C2T(Float c) const { return c * m_Value; }
            Float T2C(Float t) const { return t / m_Value; }

            Float ToCR(Float rcr) const {
                return C2T(rcr);
            }

            static Float get_k150()
            {
                Float val = 3;
                val.m_Order--; // 3/2
                return val;
            }

            static Float get_k110()
            {
                Float val = 72090;
                val.m_Order -= 16; // 72090/2^16
                return val;
            }

            static Float get_k100eps()
            {
                Float val = 65864;
                val.m_Order -= 16; // 65864/2^16 = (approx) 100.5%
                return val;
            }

            static Float get_k100()
            {
                return Float(1u);
            }

            bool IsBelow(const Pair& p, Float k) const
            {
                // theoretical formula: p.Col / p.Tok * m_Value < k
                // rewrite it as: p.Col * m_Value < p.Tok * k
                return C2T(p.Col) < (Float(p.Tok) * k);
            }

            bool IsRecovery(const Pair& totals) const
            {
                return IsBelow(totals, Price::get_k150());
            }

        };

        bool IsRecovery(const Price& price) const
        {
            return price.IsRecovery(m_Troves.m_Totals);
        }

        bool IsTroveUpdInvalid(const Trove& t, const Pair& totals0, const Price& price, bool bRecovery) const
        {
            if (bRecovery)
            {
                if (m_Troves.m_Totals.CmpRcr(totals0) < 0)
                    return true; // Ban txs that decrease the tcr.

                if (!totals0.Tok)
                    return true; // The very 1st trove drives us into recovery
            }

            return price.IsBelow(t.m_Amounts, Price::get_k110());
        }

        Amount get_BorrowFee(Amount tok, Amount tok0, bool bRecovery)
        {
            // during recovery borrowing fee is OFF
            if (bRecovery || (tok <= tok0))
                return 0;

            Amount valMinted = tok - tok0;
            Amount feeTokMin = valMinted / 200; // 0.5 percent
            Amount feeTokMax = valMinted / 20; // 5 percent

            m_BaseRate.Decay();
            Amount feeTok = feeTokMin + m_BaseRate.m_k * Float(valMinted);
            return std::min(feeTok, feeTokMax);
        }

        struct Liquidator
        {
            Price m_Price;
            FlowPair m_fpLogic;
            bool m_Stab = false;
        };

        bool LiquidateTrove(Trove& t, const Pair& totals0, Liquidator& ctx, Amount& valSurplus)
        {
            assert(t.m_Amounts.Tok >= m_Settings.m_TroveLiquidationReserve);
            bool bUseRedistPool = true;

            auto cr = ctx.m_Price.ToCR(t.m_Amounts.get_Rcr());
            if (cr > Global::Price::get_k100())
            {
                bool bAboveMcr = (cr >= Global::Price::get_k110());
                if (bAboveMcr)
                {
                    if (!ctx.m_Price.IsRecovery(totals0)) // in recovery mode can liquidate the weakest
                        return false;

                    Amount valColMax = ctx.m_Price.T2C(Float(t.m_Amounts.Tok) * Price::get_k110());
                    assert(valColMax <= t.m_Amounts.Col);
                    if (valColMax < t.m_Amounts.Col) // should always be true, just for more safety
                    {
                        valSurplus = t.m_Amounts.Col - valColMax;
                        t.m_Amounts.Col = valColMax;
                    }
                }

                if (m_StabPool.LiquidatePartial(t))
                    ctx.m_Stab = true;

                if (t.m_Amounts.Tok || t.m_Amounts.Col)
                {
                    if (bAboveMcr)
                        return false;
                }
                else
                    bUseRedistPool = false;

            }

            if (bUseRedistPool)
            {
                if (!m_RedistPool.Liquidate(t))
                    return false; // last trove?

                Strict::Add(m_Troves.m_Totals.Tok, t.m_Amounts.Tok);
                Strict::Add(m_Troves.m_Totals.Col, t.m_Amounts.Col);
            }

            ctx.m_fpLogic.Tok.Add(m_Settings.m_TroveLiquidationReserve, 0); // goes to the liquidator
            return true;
        }

        struct Redeemer
        {
            Price m_Price;
            FlowPair m_fpLogic;
            Amount m_TokRemaining;
            bool m_CrTested = false;
        };

        bool RedeemTrove(Trove& t, Redeemer& ctx) const
        {
            assert(ctx.m_TokRemaining && (t.m_Amounts.Tok >= m_Settings.m_TroveLiquidationReserve));

            if (!ctx.m_CrTested)
            {
                ctx.m_CrTested = true;

                auto cr = ctx.m_Price.ToCR(t.m_Amounts.get_Rcr());
                if (cr < Price::get_k100eps())
                    return false;
            }

            Amount valTok = t.m_Amounts.Tok - m_Settings.m_TroveLiquidationReserve;

            bool bFullRedeem = (ctx.m_TokRemaining >= valTok);
            if (!bFullRedeem)
                valTok = ctx.m_TokRemaining;

            Amount valCol = ctx.m_Price.T2C(valTok);
            assert(t.m_Amounts.Col >= valCol);

            t.m_Amounts.Col -= valCol;
            if (bFullRedeem)
            {
                t.m_Amounts.Tok = 0;
                ctx.m_TokRemaining -= valTok;
            }
            else
            {
                t.m_Amounts.Tok -= valTok;
                ctx.m_TokRemaining = 0;
            }

            ctx.m_fpLogic.Tok.Add(valTok, 1);
            ctx.m_fpLogic.Col.Add(valCol, 0);
            
            return true;
        }

        Amount AddRedeemFee(Redeemer& ctx)
        {
            if (!ctx.m_fpLogic.Tok.m_Val)
                return 0;

            Amount feeBase = ctx.m_fpLogic.Col.m_Val / 200; // redemption fee floor is 0.5 percent

            // update dynamic redeem ratio 
            m_BaseRate.Decay();
            Float kDrainRatio = Float(ctx.m_fpLogic.Tok.m_Val) / Float(ctx.m_fpLogic.Tok.m_Val + m_Troves.m_Totals.Tok);
            m_BaseRate.m_k = m_BaseRate.m_k + kDrainRatio;


            Amount fee = feeBase + m_BaseRate.m_k * Float(ctx.m_fpLogic.Col.m_Val);
            fee = std::min(fee, ctx.m_fpLogic.Col.m_Val); // fee can go as high as 100 percents

            return fee;
        }


        struct BaseRate
        {
            Float m_k;
            Height m_hLastDecay;

            void Decay()
            {
                Decay(Env::get_Height());
            }

            void Decay(Height h)
            {

                if (m_hLastDecay < h)
                {
                    if (!m_k.IsZero())
                    {
                        // decay rate is 1 percent in 60 blocks, i.e. 1/6K / block, approximately 11185 * 2^-26
                        Float kRate = 11185;
                        kRate.m_Order -= 26;

                        Float kDiff = kRate * Float(h - m_hLastDecay);

                        if (m_k > kDiff)
                            m_k = m_k - kDiff;
                        else
                            m_k.Set0();
                   }

                   m_hLastDecay = h;
                }
            }

        } m_BaseRate;

    };

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0;

            Upgradable3::Settings m_Upgradable;
            Settings m_Settings;
        };

        struct BaseTx {
            FlowPair m_Flow;
        };

        struct BaseTxUser :public BaseTx {
            PubKey m_pkUser;
        };

        struct BaseTxUserGov :public BaseTxUser {
            Amount m_GovPull;
        };

        struct BaseTxTrove :public BaseTx {
            Trove::ID m_iPrev0;
        };

        struct TroveOpen :public BaseTxUser
        {
            static const uint32_t s_iMethod = 3;
            Pair m_Amounts;
            Trove::ID m_iPrev1;
        };

        struct TroveClose :public BaseTxTrove
        {
            static const uint32_t s_iMethod = 4;
        };

        struct TroveModify :public BaseTxTrove
        {
            static const uint32_t s_iMethod = 5;
            Pair m_Amounts;
            Trove::ID m_iPrev1;
        };

        struct FundsAccess :public BaseTxUserGov
        {
            static const uint32_t s_iMethod = 6;
        };

        struct UpdStabPool :public BaseTxUserGov
        {
            static const uint32_t s_iMethod = 7;
            Amount m_NewAmount;
        };

        struct Liquidate :public BaseTxUser
        {
            static const uint32_t s_iMethod = 8;
            uint32_t m_Count;
        };

        struct Redeem :public BaseTxUser
        {
            static const uint32_t s_iMethod = 9;
            Amount m_Amount;
            Trove::ID m_iPrev1;
        };

        struct AddStabPoolReward
        {
            static const uint32_t s_iMethod = 10;
            Amount m_Amount;
        };

    } // namespace Method
#pragma pack (pop)

}
