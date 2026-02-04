#include "hal_mailbox.h"

bool hal_mailbox_call(uint32_t channel, void *message)
{
    (void)channel;
    (void)message;
    return false;
}
