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

    BlockPtr newBlock = createBlock(headBlock.header);
    
    ByteBuffer input { 1, 2, 3, 4, 56 };
    newBlock->header.serializeTo(input);
    uint256_t nonce{1, 2, 4};

    auto solution = equi::getSolution(input, nonce);
    if (equi::isValidProof(input, *solution)) 
    {
        newBlock->m_ProofOfWork = std::move(solution);
        m_blockChain.processBlock(std::move(newBlock));
    }
}

BlockPtr Miner::createBlock(const Block::Header& prevHeader)
{
    BlockPtr block = std::make_unique<Block>();
    // TODO: get transactions
    // TODO: calc merkle root hash
    
    return block;
}

}
