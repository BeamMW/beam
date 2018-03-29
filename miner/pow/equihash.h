#pragma once

#include "core/common.h"
#include <vector>
#include <array>
#include <cstdint>
#include <functional>

namespace equi
{

using ByteBuffer = std::vector<uint8_t>;
using Cancel = std::function<bool()>;

beam::Proof get_solution(const ByteBuffer &input, const beam::uint256_t &initial_nonce, const Cancel = []{ return false; });
bool is_valid_proof(const ByteBuffer &input, const beam::Proof &proof);

}
