#pragma once

#include <iostream>
#include <string>
#include "utils.hpp"
#include "models/models.hpp"

#include <google/protobuf/message.h>
using namespace google::protobuf;

#include "messages-management.pb.h"
#include "messages-common.pb.h"
#include "messages-beam.pb.h"
#include "messages.pb.h"
using namespace hw::trezor::messages::management;
using namespace hw::trezor::messages::beam;
using namespace hw::trezor::messages::common;
using namespace hw::trezor::messages;

template <typename T>
void print_bin(T msg, size_t size)
{
  for (size_t i = 0; i < size; i++)
    printf("%02x", msg[i]);
  printf("\n");
}

template <typename T>
void print_bin(T msg)
{
  print_bin(msg, msg.size());
}

void print_call_response(const Call &value)
{
  std::cout << "call.error = " << value.error << std::endl;
  std::cout << "call.type = " << value.type
            << " (" << get_message_type_name(value.type) << ")" << std::endl;
  std::cout << "call.length = " << value.length << std::endl;
  std::cout << "call.msg = ";
  print_bin(value.msg);
}
