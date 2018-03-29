#pragma once

#include "common.h"

namespace beam
{

class Input
{
    // /// The features of the output being spent.
    // /// We will check maturity for coinbase output.
    // pub features: OutputFeatures,
    // /// The commit referencing the output being spent.
    // pub commit: Commitment,
    // /// The hash of the block the output originated from.
    // /// Currently we only care about this for coinbase outputs.
    // pub block_hash: Option<Hash>,
    // /// The Merkle Proof that shows the output being spent by this input
    // /// existed and was unspent at the time of this block (proof of inclusion in output_root)
    // pub merkle_proof: Option<MerkleProof>,
};

class Output
{
    // /// Options for an output's structure or use
    // pub features: OutputFeatures,
    // /// The homomorphic commitment representing the output amount
    // pub commit: Commitment,
    // /// The switch commitment hash, a 256 bit length blake2 hash of blind*J
    // pub switch_commit_hash: SwitchCommitHash,
    // /// A proof that the commitment is in the right range
    // pub proof: RangeProof,
};

class TxKernel
{
    // /// Options for a kernel's structure or use
    // pub features: KernelFeatures,
    // /// Fee originally included in the transaction this proof is for.
    // pub fee: u64,
    // /// This kernel is not valid earlier than lock_height blocks
    // /// The max lock_height of all *inputs* to this transaction
    // pub lock_height: u64,
    // /// Remainder of the sum of all transaction commitments. If the transaction
    // /// is well formed, amounts components should sum to zero and the excess
    // /// is hence a valid public key.
    // pub excess: Commitment,
    // /// The signature proving the excess is a valid public key, which signs
    // /// the transaction fee.
    // pub excess_sig: Signature,
};

using Inputs = std::vector<Input>;
using Outputs = std::vector<Output>;
using Kernels = std::vector<TxKernel>;

class Transaction
{
public:
    Inputs inputs;
    Outputs outputs;
    Kernels kernels;
};

}