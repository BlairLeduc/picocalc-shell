/* hardware/watchdog.h on the host: see hardware/structs/watchdog.h. */
#ifndef SHELL_FAKE_WATCHDOG_H
#define SHELL_FAKE_WATCHDOG_H

#include <stdint.h>

void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);

#endif /* SHELL_FAKE_WATCHDOG_H */
