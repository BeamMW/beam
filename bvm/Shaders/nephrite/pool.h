#pragma once
#include "../Math.h"

#pragma pack (push, 1)
struct HomogenousPool
{
    typedef MultiPrecision::Float Float;

    static Amount Round(Float x)
    {
        assert(x.m_Order <= 0); // assume overflow won't happen

        Float half;
        half.m_Num = Float::s_HiBit;
        half.m_Order = 0 - Float::s_Bits;

        return x + half;
    }

    struct Base
    {
        Float m_Weight;

        struct Dim
        {
            Amount m_Buy;
            Float m_Sigma;
        };

        void TradeBase(Amount valB, Dim& dim) const
        {
            if (valB)
            {
                assert(!m_Weight.IsZero());
                dim.m_Sigma += Float(valB) / m_Weight;
                Strict::Add(dim.m_Buy, valB);
            }
        }

        struct UserBase
        {
            Float m_Weight;

            void CalcReward_(Amount& resMax, Float x) const
            {
                Amount val = Round(m_Weight * x);
                resMax = std::min(resMax, val);
            }
        };

    };

    enum Mode {
        Neutral, // s doesn't change during the trade (i.e. farming)
        Burn, // s is decreased during the trade (i.e. exchange, s burns-out)
        Grow, // s grows during the trade (i.e. redistribution)
    };

    struct Epoch0
        :public Base
    {
        uint32_t m_Users;
        Amount m_Sell;

        template <Mode m>
        void Trade0_(Amount valS, Amount valB, Dim& dim)
        {
            TradeBase(valB, dim);

            if constexpr (Mode::Burn == m)
            {
                assert(m_Sell >= valS);
                m_Sell -= valS;
            }
            else
            {
                if constexpr (Mode::Grow == m)
                    Strict::Add(m_Sell, valS);
            }
        }

        struct User0
            :public UserBase
        {
            void Add0_(Epoch0& e, Amount valSell)
            {
                assert(valSell);
                m_Weight = valSell;

                if (e.m_Users)
                {
                    m_Weight *= e.m_Weight / Float(e.m_Sell);
                    e.m_Weight += m_Weight;

                    e.m_Users++; // won't overflow, 4bln isn't feasible
                    Strict::Add(e.m_Sell, valSell);
                }
                else
                {
                    // the very 1st user. Define scale == 1
                    e.m_Weight = m_Weight;
                    e.m_Users = 1;
                    e.m_Sell = valSell;
                }
            }
        };

        int32_t EstimateScaleOrder() const
        {
            // The scale is (m_Sell / m_Weight), but we need only the order estimate
            return BitUtils::FindHiBit(m_Sell) - m_Weight.m_Order - Float::s_Bits;
        }

        bool ShouldSwitchEpoch() const
        {
            if (!m_Sell)
                return !!m_Users;

            const uint32_t nThreshold = 10; // we assume epoch switch necessary if the scale changed by at least 10 binary orders (times 1024)

            uint32_t val = EstimateScaleOrder() + nThreshold;
            return (val > nThreshold * 2);
        }
    };


    template <uint32_t nDims>
    struct Epoch
        :public Epoch0
    {
        Dim m_pDim[nDims];

        template <Mode m, uint32_t iDim>
        void Trade_(Amount valS, Amount valB)
        {
            static_assert(iDim < nDims);
            Trade0_<m>(valS, valB, m_pDim[iDim]);
        }

        void Reset()
        {
            _POD_(*this).SetZero();
        }

        struct User
            :public User0
        {
            Float m_pSigma0[nDims];

            struct Out
            {
                Amount m_Sell;
                Amount m_pBuy[nDims];
            };

            void Add_(Epoch& e, Amount valSell)
            {
                Add0_(e, valSell);

                for (uint32_t i = 0; i < nDims; i++)
                    m_pSigma0[i] = e.m_pDim[i].m_Sigma;
            }

            void DelRO_(const Epoch& e, Out& v) const
            {
                v.m_Sell = e.m_Sell;
                for (uint32_t i = 0; i < nDims; i++)
                    v.m_pBuy[i] = e.m_pDim[i].m_Buy;

                assert(e.m_Users);
                if (1 != e.m_Users)
                {
                    CalcReward_(v.m_Sell, Float(v.m_Sell) / e.m_Weight);

                    for (uint32_t i = 0; i < nDims; i++)
                        CalcReward_(v.m_pBuy[i], e.m_pDim[i].m_Sigma - m_pSigma0[i]);
                }
            }

            template <bool bReadOnly>
            void Del_(Epoch& e, Out& v) const
            {
                DelRO_(e, v);

                if constexpr (!bReadOnly)
                {
                    if (--e.m_Users)
                    {
                        e.m_Sell -= v.m_Sell;
                        for (uint32_t i = 0; i < nDims; i++)
                            e.m_pDim[i].m_Buy -= v.m_pBuy[i];

                        // reduce the weight. Beware of overflow!
                        if (e.m_Weight > m_Weight)
                        {
                            e.m_Weight -= m_Weight;
                            assert(!e.m_Weight.IsZero());
                        }
                        else
                        {
                            // actually if it happens and still pool users - this is a sign of a problem
                            // Put some minimal eps, just don't let the it be completely 0, to prevent artifacts during trade
                            e.m_Weight.m_Num = m_Weight.m_Num;
                            e.m_Weight.m_Order = m_Weight.m_Order - 1024;
                        }
                    }
                    else
                        e.Reset();
                }
            }
        };
    };

    template <uint32_t nDims>
    struct SingleEpoch
    {
        typedef HomogenousPool::Mode Mode;

        Epoch<nDims> m_Active;

        Amount get_TotalSell() const {
            return m_Active.m_Sell;
        }

        typedef typename Epoch<nDims>::User User;

        void UserAdd(User& u, Amount valSell)
        {
            u.Add_(m_Active, valSell);
        }

        template <bool bReadOnly = false>
        void UserDel(User& u, typename User::Out& out)
        {
            u.template Del_<bReadOnly>(m_Active, out);
        }

        template <Mode m, uint32_t iDim>
        void Trade(Amount valS, Amount valB)
        {
            assert(valS);
            m_Active.template Trade_<m, iDim>(valS, valB);
        }
    };

    template <uint32_t nDims>
    struct MultiEpoch
    {
        typedef HomogenousPool::Mode Mode;

        Epoch<nDims> m_Active;
        Epoch<nDims> m_Draining;

        Amount get_TotalSell() const {
            // won't overflow, we test for overflow when user joins
            return m_Active.m_Sell + m_Draining.m_Sell;
        }

        uint32_t m_iActive;

        struct User
            :public Epoch<nDims>::User
        {
            uint32_t m_iEpoch;
        };

        void UserAdd(User& u, Amount valSell)
        {
            u.m_iEpoch = m_iActive;
            u.Add_(m_Active, valSell);

            Env::Halt_if(get_TotalSell() < m_Active.m_Sell); // overflow test
        }

        template <bool bReadOnly = false, class Storage>
        void UserDel(const User& u, typename User::Out& out, Storage& stor)
        {
            if (u.m_iEpoch == m_iActive)
                u.template Del_<bReadOnly>(m_Active, out);
            else
            {
                if (u.m_iEpoch + 1 == m_iActive)
                    u.template Del_<bReadOnly>(m_Draining, out);
                else
                {
                    Epoch<nDims> e;
                    stor.Load(u.m_iEpoch, e);

                    u.template Del_<bReadOnly>(e, out);

                    if constexpr (!bReadOnly)
                    {
                        if (e.m_Users)
                            stor.Save(u.m_iEpoch, e);
                        else
                            stor.Del(u.m_iEpoch);
                    }
                }
            }
        }

        template <Mode m, uint32_t iDim>
        void Trade(Amount valS, Amount valB)
        {
            assert(valS);

            // Active epoch must always be valid
            // Account for draining epoch iff not empty
            if (m_Draining.m_Sell)
            {
                Amount totalSell = get_TotalSell();
                assert(valS <= totalSell);

                Float kRatio = Float(m_Active.m_Sell) / Float(totalSell);
                Amount s0 = Float(valS) * kRatio;
                Amount b0 = Float(valB) * kRatio;

                m_Active.template Trade_<m, iDim>(s0, b0);
                m_Draining.template Trade_<m, iDim>(valS - s0, valB - b0);
            }
            else
                m_Active.template Trade_<m, iDim>(valS, valB);
        }

        template <class Storage>
        bool MaybeSwitchEpoch(Storage& stor)
        {
            if (!m_Active.ShouldSwitchEpoch())
                return false;

            if (m_Draining.m_Users)
                stor.Save(m_iActive - 1, m_Draining);

            _POD_(m_Draining) = m_Active;
            _POD_(m_Active).SetZero();

            m_iActive++;

            return true;
        }
    };
};

template <typename TWeight, typename TValue, uint32_t nDims>
struct StaticPool
{
    typedef MultiPrecision::Float Float;

    TWeight m_Weight;
    TValue m_pValue[nDims];
    Float m_pSigma[nDims];

    void AddValue(TValue v, uint32_t iDim)
    {
        // dSigma = d / s0
        m_pSigma[iDim] = m_pSigma[iDim] + Float(v) / Float(m_Weight);
        Strict::Add(m_pValue[iDim], v);
    }

    bool IsEmpty() const {
        return !m_Weight;
    }

    void Reset()
    {
        _POD_(*this).SetZero();
    }

    struct User
    {
        TWeight m_Weight;
        Float m_pSigma0[nDims];
    };

    void Add(User& u)
    {
        Strict::Add(m_Weight, u.m_Weight);

        for (uint32_t i = 0; i < nDims; i++)
            u.m_pSigma0[i] = m_pSigma[i];
    }

    void Remove(TValue* pRet, const User& u)
    {
        if (m_Weight == u.m_Weight)
        {
            for (uint32_t i = 0; i < nDims; i++)
                pRet[i] = m_pValue[i];

            Reset();
        }
        else
        {
            assert(m_Weight > u.m_Weight);
            m_Weight -= u.m_Weight;
            Float w(u.m_Weight);

            for (uint32_t i = 0; i < nDims; i++)
            {
                pRet[i] = HomogenousPool::Round(w * (m_pSigma[i] - u.m_pSigma0[i]));
                pRet[i] = std::min(pRet[i], m_pValue[i]);
                m_pValue[i] -= pRet[i];
            }
        }
    }
};


#pragma pack (pop)
