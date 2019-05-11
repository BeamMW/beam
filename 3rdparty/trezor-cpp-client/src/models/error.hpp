#pragma once

#include <string>
#include "json.hpp"

typedef struct
{
  std::string error = {};
} Error;

inline void from_json(const nlohmann::json &j, Error &value)
{
  if (j.contains("error"))
    value.error = j.at("error").get<std::string>();
}
