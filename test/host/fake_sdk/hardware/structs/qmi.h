/* hardware/structs/qmi.h on the host: for include/picocalc_shell.h, whose
 * test (test_ds3231) uses only its clock. */
#ifndef SHELL_FAKE_QMI_H
#define SHELL_FAKE_QMI_H

#include <stdint.h>

typedef struct {
    uint32_t atrans[8];
} qmi_hw_t;

extern qmi_hw_t fake_qmi;
#define qmi_hw (&fake_qmi)
#define QMI_ATRANS0_BASE_BITS 0x00000fffu

#endif /* SHELL_FAKE_QMI_H */
