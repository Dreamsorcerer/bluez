//
// Created by ruundii on 20/07/2020.
//

#ifndef BLUEZ_INPUT_HOST_H
#define BLUEZ_INPUT_HOST_H
#include "lib/bluetooth.h"
#include <glib.h>
#include <errno.h>
#include <gmodule.h>

#include "input_common.h"

#include "btio/btio.h"
#include "gdbus/gdbus.h"
#include "src/dbus-common.h"
#include "src/shared/uhid.h"
#include <unistd.h>
#include "hidp_defs.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#define L2CAP_PSM_HIDP_CTRL	0x11
#define L2CAP_PSM_HIDP_INTR	0x13

struct input_host{
    struct btd_device	*device;
    char			*path;
    bdaddr_t		src;
    bdaddr_t		dst;
    char            dst_address[18];
    GIOChannel		*ctrl_io_remote_connection;
    GIOChannel		*intr_io_remote_connection;
    guint			ctrl_io_remote_connection_watch;
    guint			intr_io_remote_connection_watch;
    bool            dbus_interface_registered;
    gchar			*socket_path_ctrl;
    gchar			*socket_path_intr;
    GIOChannel      *ctrl_io_local_listener;
    guint           ctrl_io_local_listener_watch;
    GIOChannel      *ctrl_io_local_connection;
    guint           ctrl_io_local_connection_watch;
    GIOChannel      *intr_io_local_listener;
    guint           intr_io_local_listener_watch;
    GIOChannel      *intr_io_local_connection;
    guint           intr_io_local_connection_watch;
    gint64			reconnect_attempt_start;


/*
	guint			sec_watch;

    enum reconnect_mode_t	reconnect_mode;
    guint			reconnect_timer;
    uint32_t		reconnect_attempt;
    struct bt_uhid		*uhid;
    bool			uhid_created;
    uint8_t			report_req_pending;
    guint			report_req_timer;
    uint32_t		report_rsp_id;*/

};

int input_host_set_channel(const bdaddr_t *src, const bdaddr_t *dst, int psm, GIOChannel *io);
int input_host_remove(const bdaddr_t *src, const bdaddr_t *dst);
void ih_shutdown_channels(struct input_host *host);
int input_host_reconnect(struct input_host *host);

#endif //BLUEZ_INPUT_HOST_H
