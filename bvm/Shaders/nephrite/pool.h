#pragma once
#include "../Math.h"

#pragma pack (push, 1)
struct HomogenousPool
{
    typedef MultiPrecision::Float Float;

    struct Scale
    {
        static const uint32_t s_Initial = 64;
        static const uint32_t s_Threshold = 20; // 1mln
    };

    static Amount Round(Float x)
    {
        assert(x.m_Order <= 0); // assume overflow won't happen

        Float half;
        half.m_Num = Float::s_HiBit;
        half.m_Order = 0 - Float::s_Bits;

        return x + half;
    }

    enum Mode {
        Neutral, // s doesn't change during the trade (i.e. farming)
        Burn, // s is decreased during the trade (i.e. exchange, s burns-out)
        Grow, // s grows during the trade (i.e. redistribution)
    };

    struct Epoch0
    {
        uint32_t m_Users;
        Amount m_Sell;
        Float m_kScale;

        struct Dim
        {
            Amount m_Buy;
            Float m_Sigma;
        };

        template <Mode m>
        void Trade0_(Amount valS, Amount valB, Dim& dim)
        {
            if (!m_Sell)
            {
                assert(!valS && !valB);
                return;
            }

            Float kScale_div_s = m_kScale / Float(m_Sell);

            if (valB)
            {
                // dSigma = m_kScale * db / s
                dim.m_Sigma = dim.m_Sigma + kScale_div_s * Float(valB);

                Strict::Add(dim.m_Buy, valB);
            }

            if (valS)
            {
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

                if constexpr (Mode::Neutral != m)
                    // m_kScale *= sNew / sOld;
                    m_kScale = kScale_div_s * Float(m_Sell);
            }
        }

        struct User0
        {
            Float m_Sigma0;
            Float m_SellScaled;

            void Add0_(Epoch0& e, Amount valSell)
            {
                assert(valSell);

                m_SellScaled = Float(valSell) / e.m_kScale;

                Strict::Add(e.m_Sell, valSell);
                e.m_Users++; // won't overflow, 4bln isn't feasible
            }

            Amount CalcComponent_(Amount threshold, Float x) const
            {
                Amount res = Round(m_SellScaled * x);
                return std::min(res, threshold);
            }
        };
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
                assert(e.m_Users);
                bool bLast = (1 == e.m_Users);

                v.m_Sell = bLast ? e.m_Sell : CalcComponent_(e.m_Sell, e.m_kScale);

                for (uint32_t i = 0; i < nDims; i++)
                    v.m_pBuy[i] = bLast ? e.m_pDim[i].m_Buy : CalcComponent_(e.m_pDim[i].m_Buy, e.m_pDim[i].m_Sigma - m_pSigma0[i]);
            }

            template <bool bReadOnly>
            void Del_(Epoch& e, Out& v) const
            {
                DelRO_(e, v);

                if constexpr (!bReadOnly)
                {
                    e.m_Users--;
                    e.m_Sell -= v.m_Sell;

                    for (uint32_t i = 0; i < nDims; i++)
                        e.m_pDim[i].m_Buy -= v.m_pBuy[i];
                }
            }
        };

        bool IsUnchanged(const User& u) const
        {
            for (uint32_t i = 0; i < nDims; i++)
                if (u.m_pSigma0[i] != m_pDim[i].m_Sigma)
                    return false;
            return true;
        }

    };

    template <uint32_t nDims>
    struct SingleEpoch
    {
        typedef HomogenousPool::Mode Mode;

        Epoch<nDims> m_Active;

        Amount get_TotalSell() const {
            return m_Active.m_Sell;
        }

        void Reset()
        {
            _POD_(*this).SetZero();
            m_Active.m_kScale = 1u;
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
            if constexpr (!bReadOnly)
            {
                if (!m_Active.m_Users)
                    Reset();
            }
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

        void Init()
        {
            _POD_(*this).SetZero();
            ResetActiveScale();
            m_iActive = 1;
        }

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
        void UserDel(User& u, typename User::Out& out, Storage& stor)
        {
            if (u.m_iEpoch == m_iActive)
            {
                u.template Del_<bReadOnly>(m_Active, out);
                if constexpr (!bReadOnly)
                {
                    if (!m_Active.m_Users)
                        ResetActive();
                }
            }
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
        void OnPostTrade(Storage& stor)
        {
            static_assert(Scale::s_Initial > Scale::s_Threshold * 2); // this means that order==0 is also covered

            if (m_Active.m_kScale.m_Order < (int32_t) (Scale::s_Initial - Scale::s_Threshold))
            {
                if (m_Draining.m_Users)
                    stor.Save(m_iActive - 1, m_Draining);

                _POD_(m_Draining) = m_Active;

                ResetActive();
                m_iActive++;
            }
        }

    private:

        void ResetActive()
        {
            _POD_(m_Active).SetZero();
            ResetActiveScale();
        }

        void ResetActiveScale()
        {
            m_Active.m_kScale.m_Num = m_Active.m_kScale.s_HiBit;
            m_Active.m_kScale.m_Order = Scale::s_Initial;
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
