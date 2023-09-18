//
// Created by ruundii on 25/07/2020.
//

#ifndef BLUEZ_DEVICE_LOCAL_CHANNELS_H
#define BLUEZ_DEVICE_LOCAL_CHANNELS_H
#include <unistd.h>
#include "btio/btio.h"
#include "lib/bluetooth.h"
#include <glib.h>
#include <errno.h>
#include <gmodule.h>
#include "btio/btio.h"
#include "gdbus/gdbus.h"
#include "src/dbus-common.h"
#include "src/shared/uhid.h"
#include <unistd.h>
#include "hidp_defs.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "input_common.h"
#include "device.h"
GIOChannel *id_create_local_listening_sockets(struct input_device *device, bool is_control);
bool id_send_data_to_local(GIOChannel *chan, const uint8_t *data, size_t size);

void id_shutdown_local_connections(struct input_device *device);
void id_shutdown_local_listeners(struct input_device *device);
#endif //BLUEZ_DEVICE_LOCAL_CHANNELS_H
