#pragma once

#include "core/common.h"

namespace beam
{
    struct Wallet
    {
        struct Config
        {

        };

        struct Sender
        {
            void sendInvitation();
            void sendConfirmation();
        };

        struct Receiver
        {
            void handleInvitation();
            void handleConfirmation();
        };

        // TODO: remove this, just for test
        void sendDummyTransaction();
    };
}
