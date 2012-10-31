/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2006-2010  Nokia Corporation
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/sdp.h>
#include <bluetooth/mgmt.h>

#include <glib.h>
#include <dbus/dbus.h>

#include "log.h"

#include "adapter.h"
#include "manager.h"
#include "device.h"
#include "error.h"
#include "dbus-common.h"
#include "agent.h"
#include "storage.h"
#include "event.h"

static gboolean get_adapter_and_device(const bdaddr_t *src, bdaddr_t *dst,
					struct btd_adapter **adapter,
					struct btd_device **device,
					gboolean create)
{
	char peer_addr[18];

	*adapter = manager_find_adapter(src);
	if (!*adapter) {
		error("Unable to find matching adapter");
		return FALSE;
	}

	ba2str(dst, peer_addr);

	if (create)
		*device = adapter_get_device(*adapter, peer_addr);
	else
		*device = adapter_find_device(*adapter, peer_addr);

	if (create && !*device) {
		error("Unable to get device object!");
		return FALSE;
	}

	return TRUE;
}

/*****************************************************************
 *
 *  Section reserved to HCI commands confirmation handling and low
 *  level events(eg: device attached/dettached.
 *
 *****************************************************************/

static void pincode_cb(struct agent *agent, DBusError *derr,
				const char *pincode, struct btd_device *device)
{
	struct btd_adapter *adapter = device_get_adapter(device);
	int err;

	if (derr) {
		err = btd_adapter_pincode_reply(adapter,
					device_get_address(device), NULL, 0);
		if (err < 0)
			goto fail;
		return;
	}

	err = btd_adapter_pincode_reply(adapter, device_get_address(device),
					pincode, pincode ? strlen(pincode) : 0);
	if (err < 0)
		goto fail;

	return;

fail:
	error("Sending PIN code reply failed: %s (%d)", strerror(-err), -err);
}

int btd_event_request_pin(bdaddr_t *sba, bdaddr_t *dba, gboolean secure)
{
	struct btd_adapter *adapter;
	struct btd_device *device;
	char pin[17];
	ssize_t pinlen;
	gboolean display = FALSE;

	if (!get_adapter_and_device(sba, dba, &adapter, &device, TRUE))
		return -ENODEV;

	memset(pin, 0, sizeof(pin));
	pinlen = btd_adapter_get_pin(adapter, device, pin, &display);
	if (pinlen > 0 && (!secure || pinlen == 16)) {
		if (display && device_is_bonding(device, NULL))
			return device_notify_pincode(device, secure, pin,
								pincode_cb);

		btd_adapter_pincode_reply(adapter, dba, pin, pinlen);
		return 0;
	}

	return device_request_pincode(device, secure, pincode_cb);
}

static int confirm_reply(struct btd_adapter *adapter,
				struct btd_device *device, gboolean success)
{
	return btd_adapter_confirm_reply(adapter, device_get_address(device),
						device_get_addr_type(device),
						success);
}

static void confirm_cb(struct agent *agent, DBusError *err, void *user_data)
{
	struct btd_device *device = user_data;
	struct btd_adapter *adapter = device_get_adapter(device);
	gboolean success = (err == NULL) ? TRUE : FALSE;

	confirm_reply(adapter, device, success);
}

static void passkey_cb(struct agent *agent, DBusError *err, uint32_t passkey,
			void *user_data)
{
	struct btd_device *device = user_data;
	struct btd_adapter *adapter = device_get_adapter(device);

	if (err)
		passkey = INVALID_PASSKEY;

	btd_adapter_passkey_reply(adapter, device_get_address(device),
					device_get_addr_type(device), passkey);
}

int btd_event_user_confirm(bdaddr_t *sba, bdaddr_t *dba, uint32_t passkey)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	if (!get_adapter_and_device(sba, dba, &adapter, &device, TRUE))
		return -ENODEV;

	return device_confirm_passkey(device, passkey, confirm_cb);
}

int btd_event_user_passkey(bdaddr_t *sba, bdaddr_t *dba)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	if (!get_adapter_and_device(sba, dba, &adapter, &device, TRUE))
		return -ENODEV;

	return device_request_passkey(device, passkey_cb);
}

int btd_event_user_notify(bdaddr_t *sba, bdaddr_t *dba, uint32_t passkey,
							uint8_t entered)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	if (!get_adapter_and_device(sba, dba, &adapter, &device, TRUE))
		return -ENODEV;

	return device_notify_passkey(device, passkey, entered);
}

void btd_event_simple_pairing_complete(bdaddr_t *local, bdaddr_t *peer,
								uint8_t status)
{
	struct btd_adapter *adapter;
	struct btd_device *device;
	gboolean create;

	DBG("status=%02x", status);

	create = status ? FALSE : TRUE;

	if (!get_adapter_and_device(local, peer, &adapter, &device, create))
		return;

	if (!device)
		return;

	device_simple_pairing_complete(device, status);
}

static void update_lastused(bdaddr_t *sba, bdaddr_t *dba, uint8_t dba_type)
{
	time_t t;
	struct tm *tm;

	t = time(NULL);
	tm = gmtime(&t);

	write_lastused_info(sba, dba, dba_type, tm);
}

void btd_event_device_found(bdaddr_t *local, bdaddr_t *peer, uint8_t bdaddr_type,
					int8_t rssi, bool confirm_name,
					bool legacy, uint8_t *data,
					uint8_t data_len)
{
	struct btd_adapter *adapter;

	adapter = manager_find_adapter(local);
	if (!adapter) {
		error("No matching adapter found");
		return;
	}

	adapter_update_found_devices(adapter, peer, bdaddr_type, rssi,
					confirm_name, legacy, data, data_len);
}

void btd_event_remote_name(const bdaddr_t *local, bdaddr_t *peer,
							const char *name)
{
	struct btd_adapter *adapter;
	struct btd_device *device;
	char filename[PATH_MAX + 1];
	char local_addr[18], peer_addr[18];
	GKeyFile *key_file;
	char *data, utf8_name[MGMT_MAX_NAME_LENGTH + 1];
	gsize length = 0;

	if (!g_utf8_validate(name, -1, NULL)) {
		int i;

		memset(utf8_name, 0, sizeof(utf8_name));
		strncpy(utf8_name, name, MGMT_MAX_NAME_LENGTH);

		/* Assume ASCII, and replace all non-ASCII with spaces */
		for (i = 0; utf8_name[i] != '\0'; i++) {
			if (!isascii(utf8_name[i]))
				utf8_name[i] = ' ';
		}
		/* Remove leading and trailing whitespace characters */
		g_strstrip(utf8_name);

		name = utf8_name;
	}

	if (!get_adapter_and_device(local, peer, &adapter, &device, FALSE))
		return;

	ba2str(local, local_addr);
	ba2str(peer, peer_addr);
	snprintf(filename, PATH_MAX, STORAGEDIR "/%s/cache/%s", local_addr,
			peer_addr);
	filename[PATH_MAX] = '\0';
	create_file(filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	key_file = g_key_file_new();
	g_key_file_load_from_file(key_file, filename, 0, NULL);
	g_key_file_set_string(key_file, "General", "Name", name);

	data = g_key_file_to_data(key_file, &length, NULL);
	g_file_set_contents(filename, data, length, NULL);
	g_free(data);

	g_key_file_free(key_file);

	if (device)
		device_set_name(device, name);
}

static char *buf2str(uint8_t *data, int datalen)
{
	char *buf;
	int i;

	buf = g_try_new0(char, (datalen * 2) + 1);
	if (buf == NULL)
		return NULL;

	for (i = 0; i < datalen; i++)
		sprintf(buf + (i * 2), "%2.2x", data[i]);

	return buf;
}

static int store_longtermkey(bdaddr_t *local, bdaddr_t *peer,
				uint8_t bdaddr_type, unsigned char *key,
				uint8_t master, uint8_t authenticated,
				uint8_t enc_size, uint16_t ediv, uint8_t rand[8])
{
	GString *newkey;
	char *val, *str;
	int err;

	val = buf2str(key, 16);
	if (val == NULL)
		return -ENOMEM;

	newkey = g_string_new(val);
	g_free(val);

	g_string_append_printf(newkey, " %d %d %d %d ", authenticated, master,
								enc_size, ediv);

	str = buf2str(rand, 8);
	if (str == NULL) {
		g_string_free(newkey, TRUE);
		return -ENOMEM;
	}

	newkey = g_string_append(newkey, str);
	g_free(str);

	err = write_longtermkeys(local, peer, bdaddr_type, newkey->str);

	g_string_free(newkey, TRUE);

	return err;
}

int btd_event_link_key_notify(bdaddr_t *local, bdaddr_t *peer,
				uint8_t *key, uint8_t key_type,
				uint8_t pin_length)
{
	struct btd_adapter *adapter;
	struct btd_device *device;
	uint8_t peer_type;
	int ret;

	if (!get_adapter_and_device(local, peer, &adapter, &device, TRUE))
		return -ENODEV;

	DBG("storing link key of type 0x%02x", key_type);

	peer_type = device_get_addr_type(device);

	ret = write_link_key(local, peer, peer_type, key, key_type,
								pin_length);

	if (ret == 0) {
		device_set_bonded(device, TRUE);

		if (device_is_temporary(device))
			device_set_temporary(device, FALSE);
	}

	return ret;
}

int btd_event_ltk_notify(bdaddr_t *local, bdaddr_t *peer, uint8_t bdaddr_type,
					uint8_t *key, uint8_t master,
					uint8_t authenticated, uint8_t enc_size,
					uint16_t ediv, uint8_t rand[8])
{
	struct btd_adapter *adapter;
	struct btd_device *device;
	int ret;

	if (!get_adapter_and_device(local, peer, &adapter, &device, TRUE))
		return -ENODEV;

	ret = store_longtermkey(local, peer, bdaddr_type, key, master,
					authenticated, enc_size, ediv, rand);
	if (ret == 0) {
		device_set_bonded(device, TRUE);

		if (device_is_temporary(device))
			device_set_temporary(device, FALSE);
	}

	return ret;
}

void btd_event_conn_complete(bdaddr_t *local, bdaddr_t *peer, uint8_t bdaddr_type,
						const char *name, uint32_t class)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	if (!get_adapter_and_device(local, peer, &adapter, &device, TRUE))
		return;

	update_lastused(local, peer, bdaddr_type);

	if (class != 0)
		write_remote_class(local, peer, class);

	device_set_addr_type(device, bdaddr_type);

	adapter_add_connection(adapter, device);

	if (name != NULL)
		btd_event_remote_name(local, peer, name);
}

void btd_event_conn_failed(bdaddr_t *local, bdaddr_t *peer, uint8_t status)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	DBG("status 0x%02x", status);

	if (!get_adapter_and_device(local, peer, &adapter, &device, FALSE))
		return;

	if (!device)
		return;

	if (device_is_bonding(device, NULL))
		device_cancel_bonding(device, status);

	if (device_is_temporary(device))
		adapter_remove_device(adapter, device, TRUE);
}

void btd_event_disconn_complete(bdaddr_t *local, bdaddr_t *peer)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	DBG("");

	if (!get_adapter_and_device(local, peer, &adapter, &device, FALSE))
		return;

	if (!device)
		return;

	adapter_remove_connection(adapter, device);
}

void btd_event_device_blocked(bdaddr_t *local, bdaddr_t *peer)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	if (!get_adapter_and_device(local, peer, &adapter, &device, FALSE))
		return;

	if (device)
		device_block(device, TRUE);
}

void btd_event_device_unblocked(bdaddr_t *local, bdaddr_t *peer)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	if (!get_adapter_and_device(local, peer, &adapter, &device, FALSE))
		return;

	if (device)
		device_unblock(device, FALSE, TRUE);
}

void btd_event_device_unpaired(bdaddr_t *local, bdaddr_t *peer)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	if (!get_adapter_and_device(local, peer, &adapter, &device, FALSE))
		return;

	device_set_temporary(device, TRUE);

	if (device_is_connected(device))
		device_request_disconnect(device, NULL);
	else
		adapter_remove_device(adapter, device, TRUE);
}

/* Section reserved to device HCI callbacks */

void btd_event_returned_link_key(bdaddr_t *local, bdaddr_t *peer)
{
	struct btd_adapter *adapter;
	struct btd_device *device;

	if (!get_adapter_and_device(local, peer, &adapter, &device, TRUE))
		return;

	device_set_paired(device, TRUE);
}
