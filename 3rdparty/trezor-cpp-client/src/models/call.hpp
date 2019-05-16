#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include "utils.hpp"

typedef struct
{
  uint16_t type = 0;
  uint32_t length = 0;
  std::vector<uint8_t> msg;
  std::string error = {};

  template <typename T>
  T to_message() const
  {
    T message;
    message.ParseFromArray(msg.data(), static_cast<int>(length));
    return std::move(message);
  }

  std::string to_response()
  {
    return pack_message(type, length, msg);
  }

  void from_bytes(const uint8_t *bytes)
  {
    copy_reversed(bytes, &type);
    copy_reversed(bytes + sizeof(type), &length);
    std::copy_n(bytes + sizeof(type) + sizeof(length), length, std::back_inserter(msg));
  }
} Call;