#pragma once
#include "../Math.h"

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
            Amount s0 = m_Balance.s;
            if (!s0)
            {
                assert(!d.s && !d.b);
                return;
            }

            Float s0_(s0);

            if (d.b)
            {
                // dSigma = (d.b * m_kScale) / s0
                m_Sigma = m_Sigma + Float(d.b) * m_kScale / s0_;

                Strict::Add(m_Balance.b, d.b);
            }

            if (d.s)
            {
                if constexpr (Mode::Burn == m)
                {
                    assert(d.s <= s0);
                    m_Balance.s -= d.s;
                }
                else
                {
                    if constexpr (Mode::Grow == m)
                        Strict::Add(m_Balance.s, d.s);
                }

                if constexpr (Mode::Neutral != m)
                    // m_kScale *= m_Balance.s / s0;
                    m_kScale = m_kScale * Float(m_Balance.s) / s0_;
            }
        }
    };

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
    {
        uint32_t m_iEpoch;
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


        void Del_(Epoch& e, Pair& out)
        {
            assert(e.m_Users);
            if (--e.m_Users)
            {
                out.s = std::min<Amount>(e.m_Balance.s, m_SellScaled * e.m_kScale);
                out.b = std::min<Amount>(e.m_Balance.b, m_SellScaled * (e.m_Sigma - m_Sigma0));
            }
            else
                out = e.m_Balance;

            e.m_Balance.s -= out.s;
            e.m_Balance.b -= out.b;
        }
    };

    void UserAdd(User& u, Amount valSell)
    {
        u.m_iEpoch = m_iActive;
        u.Add_(m_Active, valSell);

        Env::Halt_if(get_TotalSell() < m_Active.m_Balance.s); // overflow test
    }

    template <class IO>
    void UserDel(User& u, Pair& out)
    {
        if (u.m_iEpoch == m_iActive)
        {
            u.Del_(m_Active, out);
            if (!m_Active.m_Users)
                ResetActive();
        }
        else
        {
            if (u.m_iEpoch + 1 == m_iActive)
                u.Del_(m_Draining, out);
            else
            {
                Epoch e;
                IO::Load(u.m_iEpoch, e);

                u.Del_(e, out);

                if (e.m_Users)
                    IO::Save(u.m_iEpoch, e);
                else
                    IO::Del(u.m_iEpoch);
            }
        }
    }

    template <Mode m>
    void Trade_(const Pair& d)
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

    template <class IO>
    void OnPostTrade()
    {
        if (!Scale::IsSane(m_Active.m_kScale, Scale::s_Threshold))
        {
            UnloadDraining<IO>();

            _POD_(m_Draining) = m_Active;

            ResetActive();
            m_iActive++;
        }

        if (!Scale::IsSane(m_Draining.m_kScale, Scale::s_Threshold * 2))
        {
            UnloadDraining<IO>();
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

    template <class IO>
    void UnloadDraining()
    {
        if(m_Draining.m_Users)
            IO::Save(m_iActive - 1, m_Draining);
    }
};

struct ExchangePool
    :public HomogenousPool
{
    void Trade(const Pair& d)
    {
        Trade_<Mode::Burn>(d);
    }
};

struct DistributionPool
    :public HomogenousPool
{
    void Trade(const Pair& d)
    {
        Trade_<Mode::Grow>(d);
    }
};
