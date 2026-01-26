#include "geo_notify.h"

#include <stddef.h>

static geo_notify_cb_t s_geo_notify_cb = NULL;

void geo_notify_register(geo_notify_cb_t cb) {
    s_geo_notify_cb = cb;
}

void geo_notify(const char *msg, int frames) {
    if (s_geo_notify_cb && msg) {
        s_geo_notify_cb(msg, frames);
    }
}
