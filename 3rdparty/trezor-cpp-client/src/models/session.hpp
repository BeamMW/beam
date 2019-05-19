#pragma once

#include <string>
#include "json.hpp"

typedef struct
{
  std::string session;
  std::string error;
} Session;

inline void from_json(const nlohmann::json &j, Session &value)
{
  if (j.contains("session"))
    value.session = j.at("session").get<std::string>();

  if (j.contains("error"))
    value.error = j.at("error").get<std::string>();
}
