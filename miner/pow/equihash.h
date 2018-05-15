#pragma once

#include "core/common.h"
#include <vector>
#include <array>
#include <cstdint>
#include <functional>

namespace equi
{

using Cancel = std::function<bool()>;

beam::Block::PoWPtr getSolution(const beam::ByteBuffer& input, const beam::Block::PoW::NonceType& initialNonce, const Cancel& = []{ return false; });
bool isValidProof(const beam::ByteBuffer& input, const beam::Block::PoW& proof);

}
