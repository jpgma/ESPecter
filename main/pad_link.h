#ifndef ESPECTER_PAD_LINK_H
#define ESPECTER_PAD_LINK_H

#include "pad_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/* [pad_link_start] Sobe o NimBLE central e procura o Pad Emu. O NVS já está de pé por [nvs_start]. */
void pad_link_start(void);

/* [pad_link_read] Copia o mailbox do último report. */
void pad_link_read(pad_view *out);

#ifdef __cplusplus
}
#endif

#endif
