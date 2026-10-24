/* pico/time.h on the host: for include/picocalc_shell.h, whose test
 * (test_ds3231) uses only its clock. */
#ifndef SHELL_FAKE_TIME_H
#define SHELL_FAKE_TIME_H

#include <stdint.h>

void busy_wait_us(uint64_t us);
#define tight_loop_contents() ((void)0)

#endif /* SHELL_FAKE_TIME_H */
