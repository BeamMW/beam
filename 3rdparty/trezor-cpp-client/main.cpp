#include <iostream>
#include <string>
#include "client.hpp"
#include "queue/working_queue.h"
#include "device_manager.hpp"

#include "debug.hpp"

int main()
{
    Client client;
    std::vector<std::unique_ptr<DeviceManager>> trezors;
    std::vector<std::unique_ptr<std::atomic_flag>> is_alive_flags;

    auto enumerates = client.enumerate();

    if (enumerates.size() == 0)
    {
        std::cout << "there is no device connected" << std::endl;
        return 1;
    }

    auto clear_flag = [&](size_t queue_size, size_t idx) {
        if (queue_size == 0)
            is_alive_flags.at(idx)->clear();
    };

    for (auto enumerate : enumerates)
    {
        trezors.push_back(std::unique_ptr<DeviceManager>(new DeviceManager()));
        is_alive_flags.push_back(std::unique_ptr<std::atomic_flag>(new std::atomic_flag(1)));
        auto is_alive_idx = is_alive_flags.size() - 1;
        auto& trezor = trezors.back();

        if (enumerate.session != "null")
        {
            client.release(enumerate.session);
            enumerate.session = "null";
        }

        trezor->callback_Failure([&, is_alive_idx](const Message &msg, size_t queue_size) {
            std::cout << "FAIL REASON: " << child_cast<Message, Failure>(msg).message() << std::endl;
            clear_flag(queue_size, is_alive_idx);
        });

        trezor->callback_Success([&, is_alive_idx](const Message &msg, size_t queue_size) {
            std::cout << "SUCCESS: " << child_cast<Message, Success>(msg).message() << std::endl;
            clear_flag(queue_size, is_alive_idx);
        });

        try
        {
            trezor->init(enumerate);
            trezor->call_Ping("hello beam", true);
            trezor->call_BeamGetOwnerKey(true, [&, is_alive_idx](const Message &msg, size_t queue_size) {
                std::cout << "BEAM OWNER KEY: " << child_cast<Message, BeamOwnerKey>(msg).key() << std::endl;
                clear_flag(queue_size, is_alive_idx);
            });
            trezor->call_BeamGenerateNonce(1, [&, is_alive_idx](const Message &msg, size_t queue_size) {
                std::cout << "BEAM NONCE IN SLOT 1: ";
                print_bin(reinterpret_cast<const uint8_t*>(child_cast<Message, BeamECCImage>(msg).image_x().c_str()), 32);
                clear_flag(queue_size, is_alive_idx);
            });
            trezor->call_BeamGenerateKey(0, 0, 0, 1, true, [&, is_alive_idx](const Message &msg, size_t queue_size) {
                std::cout << "BEAM GENERATED PUBLIC KEY:" << std::endl;
                std::cout << "pub_x: ";
                print_bin(reinterpret_cast<const uint8_t*>(child_cast<Message, BeamPublicKey>(msg).pub_x().c_str()), 32);
                std::cout << "pub_y: ";
                print_bin(reinterpret_cast<const uint8_t*>(child_cast<Message, BeamPublicKey>(msg).pub_y().c_str()), 1);
                clear_flag(queue_size, is_alive_idx);
            });
        }
        catch (std::runtime_error e)
        {
            std::cout << e.what() << std::endl;
        }
    }

    for (auto& is_alive : is_alive_flags)
        while (is_alive->test_and_set())
            ; // waiting
    curl_global_cleanup();
    return 0;
}
