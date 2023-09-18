/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef BLUEZ_INPUT_DEVICE_H
#define BLUEZ_INPUT_DEVICE_H

#define L2CAP_PSM_HIDP_CTRL	0x11
#define L2CAP_PSM_HIDP_INTR	0x13

enum reconnect_mode_t {
    RECONNECT_NONE = 0,
    RECONNECT_DEVICE,
    RECONNECT_HOST,
    RECONNECT_ANY
};

struct input_device {
    struct btd_service	*service;
    struct btd_device	*device;
    char			*path;
    bdaddr_t		src;
    bdaddr_t		dst;
    uint32_t		handle;
    GIOChannel		*ctrl_io;
    GIOChannel		*intr_io;
    guint			ctrl_watch;
    guint			intr_watch;
    guint			sec_watch;
    struct hidp_connadd_req *req;
    bool			disable_sdp;
    enum reconnect_mode_t	reconnect_mode;
    guint			reconnect_timer;
    uint32_t		reconnect_attempt;
    struct bt_uhid		*uhid;
    bool			uhid_created;
    uint8_t			report_req_pending;
    guint			report_req_timer;
    uint32_t		report_rsp_id;
    bool			virtual_cable_unplug;

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

};

struct input_conn;

void input_set_idle_timeout(int timeout);
void input_enable_userspace_hid(bool state);
void input_set_classic_bonded_only(bool state);
bool input_get_classic_bonded_only(void);
void input_device_set_capture_uhid_channels_for_devices(bool state);
void input_device_set_capture_uhid_channels_for_devices_exclusively(bool state);
void input_set_auto_sec(bool state);

int input_device_register(struct btd_service *service);
void input_device_unregister(struct btd_service *service);

bool input_device_exists(const bdaddr_t *src, const bdaddr_t *dst);
int input_device_set_channel(const bdaddr_t *src, const bdaddr_t *dst, int psm,
							GIOChannel *io);
int input_device_close_channels(const bdaddr_t *src, const bdaddr_t *dst);

int input_device_connect(struct btd_service *service);
int input_device_disconnect(struct btd_service *service);

int check_if_remote_device_is_input_host(const bdaddr_t *src, const bdaddr_t *dst);

bool hidp_send_message(GIOChannel *chan, uint8_t hdr, const uint8_t *data, size_t size);

#endif //BLUEZ_INPUT_DEVICE_H
