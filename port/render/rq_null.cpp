// Null backend: no GPU objects. Step 1.1 headless checks (records + hash + compare only).
#include "rq_internal.h"

static int nb_init(void *be, const RqOptions *o) { (void)be; (void)o; return 1; }
static void nb_void(void *be) { (void)be; }

extern "C" const RqBackendOps rq_backend_null = {
    nb_init, nb_void, nb_void, nb_void,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
};
