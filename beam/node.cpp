#include "node.h"
#include "core/serialization_adapters.h"

namespace beam
{
    // temporary impl of NetworkToWallet interface
    struct NetworkToWalletDummyImpl : public NetworkToWallet
    {
        NetworkToWalletDummyImpl(NetworkToWallet& handler)
            : m_handler(handler)
        {

        }

        void handlePoolPush(const ByteBuffer& data)
        {
            Transaction tx;

            Deserializer des;
            des.reset(&data[0], data.size());

            des & tx;

            m_handler.onNewTransaction(tx);
        }

    private:
        NetworkToWallet& m_handler;
    };

    void Node::listen(const Node::Config& config)
    {
        // start node server
    }

    void Node::onNewTransaction(const Transaction& tx)
    {
        // check and add new transaction to the pool
    }
}