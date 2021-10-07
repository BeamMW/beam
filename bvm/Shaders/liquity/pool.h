#pragma once
#include "../Math.h"

struct ExchangePool
{
    typedef MultiPrecision::Float Float;

    struct Decay
    {
        static const int32_t s_Threshold1 = -20;
        static const int32_t s_Threshold2 = -40;
    };

    struct Pair {
        Amount s;
        Amount b;
    };

    struct Epoch
    {
        uint32_t m_Users;
        Pair m_Balance;
        Float m_Sigma;
        Float m_kDecay;

        void Trade_(const Pair& d)
        {
            Amount s0 = m_Balance.s;
            assert(d.s <= s0);

            if (!s0)
            {
                assert(!d.s && !d.b);
                return;
            }

            Float s0_(s0);

            // dSigma = (d.b * m_kDecay) / s0
            m_Sigma = m_Sigma + Float(d.b) * m_kDecay / s0_;

            Strict::Add(m_Balance.b, d.b);
            m_Balance.s -= d.s;

            // m_kDecay *= m_Balance.s / s0;
            m_kDecay = m_kDecay * Float(m_Balance.s) / s0_;
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
        ResetActiveDecay();
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

            assert(e.m_kDecay.m_Order >= Decay::s_Threshold1);

            m_SellScaled = Float(valSell) / e.m_kDecay;

            Strict::Add(e.m_Balance.s, valSell);
            e.m_Users++; // won't overflow, 4bln isn't feasible
        }


        void Del_(Epoch& e, Pair& out)
        {
            assert(e.m_Users);
            if (--e.m_Users)
            {
                out.s = std::min<Amount>(e.m_Balance.s, m_SellScaled * e.m_kDecay);
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

    void Trade(const Pair& d)
    {
        assert(d.s);

        // Active epoch must always be valid
        // Account for draining epoch iff not empty
        if (m_Draining.m_Users)
        {
            Amount totalSell = get_TotalSell();
            assert(d.s <= totalSell);

            // split among active/draining w.r.t. their currency proportion. Round in favor of draining
            Float kRatio = Float(m_Active.m_Balance.s) / Float(totalSell);
            Pair d0;
        
            d0.s = Float(d.s) * kRatio;
            d0.b = Float(d.b) * kRatio;

            m_Active.Trade_(d0);

            d0.s = d.s - d0.s;
            d0.b = d.b - d0.b;

            m_Draining.Trade_(d0);        }
        else
            m_Active.Trade_(d);
    }

    template <class IO>
    void OnPostTrade()
    {
        if (m_Active.m_kDecay >= Decay::s_Threshold)
            return;

        if (m_Draining.m_Users)
            IO::Save(m_iActive - 1, m_Draining);

        _POD_(m_Draining) = m_Active;

        ResetActive();
        m_iActive++;
    }

private:

    void ResetActive()
    {
        _POD_(m_Active).SetZero();
        ResetActiveDecay();
    }

    void ResetActiveDecay()
    {
        m_Active.m_kDecay.m_Num = m_Active.m_kDecay.s_HiBit;
        m_Active.m_kDecay.m_Order = 0;
    }
};

