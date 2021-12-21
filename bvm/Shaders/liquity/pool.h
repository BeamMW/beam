#pragma once
#include "../Math.h"

#pragma pack (push, 1)
struct HomogenousPool
{
    typedef MultiPrecision::Float Float;

    struct Scale
    {
        static const uint32_t s_Threshold = 20; // 1mln

        static bool IsSane(const Float& x, uint32_t nThreshold)
        {
            // should be (nThreshold > x.m_Order >= -nThreshold)
            // (2*nThreshold > x.m_Order + nThreshold >= 0)
            uint32_t val = nThreshold + x.m_Order;
            return (val < nThreshold * 2);
        }
    };

    struct Pair
    {
        Amount s;
        Amount b;

        Pair get_Fraction(const Float& kRatio) const
        {
            Pair p;
            p.s = Float(s) * kRatio;
            p.b = Float(b) * kRatio;
            return p;
        }

        Pair get_Fraction(Amount w1, Amount wTotal) const
        {
            assert(wTotal);
            return get_Fraction(Float(w1) / Float(wTotal));
        }

        Pair operator - (const Pair& p) const
        {
            Pair ret;
            ret.s = s - p.s;
            ret.b = b - p.b;
            return ret;
        }
    };

    enum Mode {
        Neutral, // s doesn't change during the trade (i.e. farming)
        Burn, // s is decreased during the trade (i.e. exchange, s burns-out)
        Grow, // s grows during the trade (i.e. redistribution)
    };

    struct Epoch
    {
        uint32_t m_Users;
        Pair m_Balance;
        Float m_Sigma;
        Float m_kScale;

        template <Mode m>
        void Trade_(const Pair& d)
        {
            if (!m_Balance.s)
            {
                assert(!d.s && !d.b);
                return;
            }

            Float kScale_div_s = m_kScale / Float(m_Balance.s);

            if (d.b)
            {
                // dSigma = m_kScale * db / s
                m_Sigma = m_Sigma + kScale_div_s * Float(d.b);

                Strict::Add(m_Balance.b, d.b);
            }

            if (d.s)
            {
                if constexpr (Mode::Burn == m)
                {
                    assert(m_Balance.s >= d.s);
                    m_Balance.s -= d.s;
                }
                else
                {
                    if constexpr (Mode::Grow == m)
                        Strict::Add(m_Balance.s, d.s);
                }

                if constexpr (Mode::Neutral != m)
                    // m_kScale *= sNew / sOld;
                    m_kScale = kScale_div_s * Float(m_Balance.s);
            }
        }

        struct User
        {
            Float m_Sigma0;
            Float m_SellScaled;

            void Add_(Epoch& e, Amount valSell)
            {
                assert(valSell);
                m_Sigma0 = e.m_Sigma;

                m_SellScaled = Float(valSell) / e.m_kScale;

                Strict::Add(e.m_Balance.s, valSell);
                e.m_Users++; // won't overflow, 4bln isn't feasible
            }

            void DelRO_(const Epoch& e, Pair& out) const
            {
                assert(e.m_Users);

                if (1 == e.m_Users)
                    out = e.m_Balance;
                else
                {
                    out.s = std::min<Amount>(e.m_Balance.s, m_SellScaled * e.m_kScale);
                    out.b = std::min<Amount>(e.m_Balance.b, m_SellScaled * (e.m_Sigma - m_Sigma0));
                }
            }

            template <bool bReadOnly>
            void Del_(Epoch& e, Pair& out) const
            {
                DelRO_(e, out);

                if constexpr (!bReadOnly)
                {
                    e.m_Users--;
                    e.m_Balance = e.m_Balance - out;
                }
            }
        };
    };

    template <Mode m>
    struct SingleEpoch
    {
        Epoch m_Active;

        Amount get_TotalSell() const {
            return m_Active.m_Balance.s;
        }

        void Reset()
        {
            _POD_(*this).SetZero();
            m_Active.m_kScale = 1u;
        }

        typedef Epoch::User User;

        void UserAdd(User& u, Amount valSell)
        {
            u.Add_(m_Active, valSell);
        }

        template <bool bReadOnly = false>
        void UserDel(User& u, Pair& out)
        {
            u.Del_<bReadOnly>(m_Active, out);
            if constexpr (!bReadOnly)
            {
                if (!m_Active.m_Users)
                    Reset();
            }
        }

        void Trade(const Pair& d)
        {
            assert(d.s);
            m_Active.Trade_<m>(d);
        }
    };


    template <Mode m>
    struct MultiEpoch
    {
        Epoch m_Active;
        Epoch m_Draining;

        Amount get_TotalSell() const {
            // won't overflow, we test for overflow when user joins
            return m_Active.m_Balance.s + m_Draining.m_Balance.s;
        }

        uint32_t m_iActive;

        void Init()
        {
            _POD_(*this).SetZero();
            ResetActiveScale();
            m_iActive = 1;
        }

        struct User
            :public Epoch::User
        {
            uint32_t m_iEpoch;
        };

        void UserAdd(User& u, Amount valSell)
        {
            u.m_iEpoch = m_iActive;
            u.Add_(m_Active, valSell);

            Env::Halt_if(get_TotalSell() < m_Active.m_Balance.s); // overflow test
        }

        template <bool bReadOnly = false, class Storage>
        void UserDel(User& u, Pair& out, Storage& stor)
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
                    Epoch e;
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

        void Trade(const Pair& d)
        {
            assert(d.s);

            // Active epoch must always be valid
            // Account for draining epoch iff not empty
            if (m_Draining.m_Users)
            {
                Amount totalSell = get_TotalSell();
                assert(d.s <= totalSell);

                Pair d0 = d.get_Fraction(m_Active.m_Balance.s, totalSell);
                m_Active.Trade_<m>(d0);

                d0 = d - d0;
                m_Draining.Trade_<m>(d0);
            }
            else
                m_Active.Trade_<m>(d);
        }

        template <class Storage>
        void OnPostTrade(Storage& stor)
        {
            if (!Scale::IsSane(m_Active.m_kScale, Scale::s_Threshold))
            {
                UnloadDraining(stor);

                _POD_(m_Draining) = m_Active;

                ResetActive();
                m_iActive++;
            }

            if (!Scale::IsSane(m_Draining.m_kScale, Scale::s_Threshold * 2))
            {
                UnloadDraining(stor);
                _POD_(m_Draining).SetZero();
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
            m_Active.m_kScale.m_Order = 0;
        }

        template <class Storage>
        void UnloadDraining(Storage& stor)
        {
            if (m_Draining.m_Users)
                stor.Save(m_iActive - 1, m_Draining);
        }
    };
};

typedef HomogenousPool::MultiEpoch<HomogenousPool::Mode::Burn> ExchangePool;
typedef HomogenousPool::SingleEpoch<HomogenousPool::Mode::Grow> DistributionPool;

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
                pRet[i] = w * (m_pSigma[i] - u.m_pSigma0[i]);
                pRet[i] = std::min(pRet[i], m_pValue[i]);
                m_pValue[i] -= pRet[i];
            }
        }
    }
};


#pragma pack (pop)
