#ifndef GEO_NOTIFY_H
#define GEO_NOTIFY_H

typedef void (*geo_notify_cb_t)(const char *msg, int frames);

void geo_notify_register(geo_notify_cb_t cb);
void geo_notify(const char *msg, int frames);

#endif // GEO_NOTIFY_H
