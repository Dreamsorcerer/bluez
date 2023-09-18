//
// Created by ruundii on 25/07/2020.
//

#ifndef BLUEZ_HOST_REMOTE_CHANNELS_H
#define BLUEZ_HOST_REMOTE_CHANNELS_H
#include "host.h"
#include "host_local_channels.h"
gboolean ih_remote_control_watch_cb(GIOChannel *chan, GIOCondition cond, gpointer data);
gboolean ih_remote_interrupt_watch_cb(GIOChannel *chan, GIOCondition cond, gpointer data);
bool ih_send_data_to_remote(GIOChannel *chan, const uint8_t *data, size_t size);
void ih_shutdown_remote_connections(struct input_host *host);

#endif //BLUEZ_HOST_REMOTE_CHANNELS_H
