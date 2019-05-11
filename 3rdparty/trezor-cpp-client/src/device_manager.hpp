#pragma once

#include <string>
#include <unordered_map>
#include "client.hpp"
#include "models/models.hpp"
#include "queue/working_queue.h"
#include "utils.hpp"

#include "debug.hpp"

class DeviceManager
{
public:
  using MessageCallback = std::function<void(const Message &, size_t)>;

  DeviceManager()
  {
    m_worker_queue.setGlobalPopCallback(
        [&](const std::string &session_pop, const Call &call) {
          if (MessageType_ButtonRequest == call.type)
          {
            m_worker_queue.push(session_pop, [&](const std::string &session) {
              return m_client.call(session, pack_message(ButtonAck()));
            });
          }
          else
          {
            auto released = m_client.release(session_pop);
            if (released.session == m_session)
              m_session = "null";

            handle_response(call);

            m_request_queue.unlockPop();
          }
        });
  }

  void init(const Enumerate &enumerate) throw()
  {
    if (enumerate.session != "null")
      throw std::runtime_error("device already occupied, complete the previous session first");

    m_path = enumerate.path;
    m_session = enumerate.session;
    m_is_real = enumerate.vendor && enumerate.product;
  }

  void call_Ping(std::string text, bool button_protection)
  {
    Ping message;
    message.set_message(text);
    message.set_button_protection(button_protection);
    call(pack_message(message));
  }

  void call_BeamGetOwnerKey(bool show_display, MessageCallback callback)
  {
    m_callbacks[MessageType_BeamOwnerKey] = callback;

    BeamGetOwnerKey message;
    message.set_show_display(show_display);
    call(pack_message(message));
  }

  void call_BeamGenerateNonce(uint8_t slot, MessageCallback callback)
  {
    m_callbacks[MessageType_BeamECCImage] = callback;

    BeamGenerateNonce message;
    message.set_slot(slot);
    call(pack_message(message));
  }

  void callback_Failure(MessageCallback callback)
  {
    m_callbacks[MessageType_Failure] = callback;
  }

  void callback_Success(MessageCallback callback)
  {
    m_callbacks[MessageType_Success] = callback;
  }

protected:
  void handle_response(const Call &call)
  {
    switch (call.type)
    {
    case MessageType_Failure:
      execute_callback<MessageType_Failure, Failure>(call);
      break;
    case MessageType_Success:
      execute_callback<MessageType_Success, Success>(call);
      break;
    case MessageType_BeamOwnerKey:
      execute_callback<MessageType_BeamOwnerKey, BeamOwnerKey>(call);
      break;
    case MessageType_BeamECCImage:
      execute_callback<MessageType_BeamECCImage, BeamECCImage>(call);
      break;
    case INTERNAL_ERROR:
    {
      print_call_response(call);

      m_session = "null";
      m_request_queue.clear();
      m_worker_queue.clear();

      Failure error;
      error.set_message(call.error);
      auto callback = m_callbacks.find(MessageType_Failure);
      if (callback != m_callbacks.end())
        callback->second(error, m_request_queue.size());
      break;
    }
    default:
      break;
    }
  }

  template <int Type, typename MessageType>
  void execute_callback(const Call &call)
  {
    auto callback = m_callbacks.find(Type);
    if (callback != m_callbacks.end())
      callback->second(call.to_message<MessageType>(), m_request_queue.size());
  }

  void call(std::string message) throw()
  {
    m_request_queue.push(m_request_queue.size(), [&, message](int size) {
      if (m_session != "null")
      {
        throw std::runtime_error("previous session must be completed");
        // return false; //TODO: decide which better (throw or return false)
      }

      auto acquired = m_client.acquire(m_path, m_session);
      if (acquired.error.empty())
      {
        m_session = acquired.session;
        m_worker_queue.push(m_session, [&, message](const std::string &session) {
          return m_client.call(session, message);
        });

        m_request_queue.lockPop();
      }
      return true;
    });
  }

private:
  Client m_client;
  WorkingQueue<Call, std::string> m_worker_queue;
  WorkingQueue<bool, size_t> m_request_queue;

  std::unordered_map<int, MessageCallback> m_callbacks;
  std::string m_path = "null";
  std::string m_session = "null";
  bool m_is_real = false;
};
