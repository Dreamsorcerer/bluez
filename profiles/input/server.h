/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 */

void set_input_device_profile_enabled(bool input_device_profile_enabled);
void set_input_device_profile_sdp_record(sdp_record_t *rec);
int server_start(const bdaddr_t *src);
void server_stop(const bdaddr_t *src);
