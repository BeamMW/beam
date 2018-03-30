#include "miner.h"

#include "pow/equihash.h"
#include "chain/chain.h"
#include "pool/txpool.h"

namespace beam
{

Miner::Miner(Chain& blockChain, TxPool& txPool)
    : m_blockChain(blockChain)
    , m_txPool(txPool)
{
}

void Miner::start()
{

}

void Miner::mineBlock()
{
    const Block& headBlock = m_blockChain.getHeadBlock();

    BlockUniquePtr newBlock = createBlock(headBlock.header);
    
    ByteBuffer input { 1, 2, 3, 4, 56 };
    newBlock->header.serializeTo(input);
    uint256_t nonce{1, 2, 4};

    Proof solution = equi::getSolution(input, nonce);
    if (equi::isValidProof(input, solution)) {
        newBlock->header.pow = std::move(solution);
        m_blockChain.processBlock(std::move(newBlock));
    }
}

BlockUniquePtr Miner::createBlock(const BlockHeader& prevHeader)
{
    BlockUniquePtr block = std::make_unique<Block>();
    // TODO: get transactions
    // TODO: calc merkle root hash
    
    return block;
}

}
