#include "node.h"

namespace beam
{
    void Node::listen(const Node::Config& config)
    {
        // start node server here and process requests from the wallet
        // parse request and call handlePoolPush() here
    }

    void Node::handlePoolPush(const ByteBuffer& data)
    {
        Transaction tx;
        tx.deserializeFrom(data);
    }
}