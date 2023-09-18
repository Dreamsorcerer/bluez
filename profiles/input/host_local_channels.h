//
// Created by ruundii on 25/07/2020.
//

#ifndef BLUEZ_HOST_LOCAL_CHANNELS_H
#define BLUEZ_HOST_LOCAL_CHANNELS_H
#include "host.h"
#include "host_remote_channels.h"

bool ih_send_data_to_local(GIOChannel *chan, const uint8_t *data, size_t size);
GIOChannel *ih_create_local_listening_sockets(struct input_host *host, bool is_control);
void ih_shutdown_local_connections(struct input_host *host);
void ih_shutdown_local_listeners(struct input_host *host);

#endif //BLUEZ_HOST_LOCAL_CHANNELS_H
