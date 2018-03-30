#pragma once

#include "common.h"
#include "transaction.h"

namespace beam
{

class BlockHeader
{
public:
    // Version of the block
    uint16_t version;

    // Height of this block since the genesis block (height 0)
    uint64_t height;

    // Timestamp at which the block was built.
    Timestamp timestamp;

    // Hash of the block previous to this in the chain.
    Hash previous;

    // Merklish root of all the commitments in the TxHashSet
    // Hash output_root;

    // Merklish root of all range proofs in the TxHashSet
    // Hash range_proof_root;

    // Merklish root of all transaction kernels in the TxHashSet
    // Hash kernel_root;

    // Total accumulated difficulty since genesis block
    Difficulty total_difficulty;

    // Total accumulated sum of kernel offsets since genesis block.
    // We can derive the kernel offset sum for *this* block from
    // the total kernel offset of the previous block header.
    // total_kernel_offset: BlindingFactor,

    // Proof of work data.
    Proof pow;

    BlockHeader(){}

    BlockHeader(const BlockHeader& other) = delete;
    BlockHeader(BlockHeader&& other) = delete;
    BlockHeader& operator=(const BlockHeader&) = delete;
    BlockHeader& operator=(BlockHeader&&) = delete;

    template<typename Buffer>
    void serializeTo(Buffer& b)
    {

    }
};

class Block
{
public:
    // The header with metadata and commitments to the rest of the data
    BlockHeader header;

    // List of transaction inputs
    Inputs inputs;

    // List of transaction outputs
    Outputs outputs;

    // List of kernels with associated proofs (note these are offset from tx_kernels)
    Kernels kernels;

    Block() {}

    Block(const Block& other) = delete;
    Block(Block&& other) = delete;
    Block& operator=(const Block&) = delete;
    Block& operator=(Block&&) = delete;

    template<typename Buffer>
    void serializeTo(Buffer& b)
    {

    }
};

using BlockUniquePtr = std::unique_ptr<Block>;

}