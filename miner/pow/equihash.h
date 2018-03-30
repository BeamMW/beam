#pragma once

#include "core/common.h"
#include <vector>
#include <array>
#include <cstdint>
#include <functional>

namespace equi
{

using Cancel = std::function<bool()>;

beam::Proof getSolution(const beam::ByteBuffer &input, const beam::uint256_t &initial_nonce, const Cancel = []{ return false; });
bool isValidProof(const beam::ByteBuffer &input, const beam::Proof &proof);

}
