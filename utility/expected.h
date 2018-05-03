#pragma once
#include "tl/expected.hpp"

using tl::expected;
using tl::unexpected;
using tl::make_unexpected;

inline expected<void,int> ok() { return expected<void,int>(); }
