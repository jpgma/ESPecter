#include "actions.h"

#include <string.h>

/* [actions_capture] Zera as actions. O device ainda não lê o pad. */
void actions_capture(actions *out)
{
    memset(out, 0, sizeof(*out));
}
