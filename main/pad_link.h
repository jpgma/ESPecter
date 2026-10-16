#ifndef ESPECTER_PAD_LINK_H
#define ESPECTER_PAD_LINK_H

#include "pad_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/* [pad_link_start] Sobe o NimBLE central, a task do BOOT e do PLUS, e procura o Pad Emu. O NVS já está de pé por [nvs_start]. */
void pad_link_start(void);
/* [pad_link_pause] Para o central e o controller. Devolve 0 se o rádio ficou quieto. */
int pad_link_pause(void);
/* [pad_link_resume] Sobe o central de novo e volta a escanear. */
void pad_link_resume(void);

/* [pad_link_read] Copia o mailbox do último report. */
void pad_link_read(pad_view *out);
/* [pad_link_take_debug] Devolve 1 uma vez quando o PLUS foi aceito. */
int pad_link_take_debug(void);
/* [pad_link_take_reset] Devolve 1 uma vez quando um BOOT sozinho fechou a espera. */
int pad_link_take_reset(void);

#ifdef __cplusplus
}
#endif

#endif
