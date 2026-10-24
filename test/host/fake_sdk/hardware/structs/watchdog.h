/* hardware/structs/watchdog.h on the host: for include/picocalc_shell.h,
 * whose test (test_ds3231) uses only its clock. */
#ifndef SHELL_FAKE_WATCHDOG_STRUCTS_H
#define SHELL_FAKE_WATCHDOG_STRUCTS_H

#include <stdint.h>

typedef struct {
    uint32_t scratch[8];
} watchdog_hw_t;

extern watchdog_hw_t fake_watchdog;
#define watchdog_hw (&fake_watchdog)

#endif /* SHELL_FAKE_WATCHDOG_STRUCTS_H */
