//
// Created by ruundii on 25/07/2020.
//

#include "host_remote_channels.h"

bool ih_send_data_to_remote(GIOChannel *chan, const uint8_t *data, size_t size)
{
    int fd;
    ssize_t len;
    uint8_t msg[size];

    if (!chan) {
        error("BT socket not connected");
        return false;
    }

    if (data == NULL)
        size = 0;

    if (size > 0)
        memcpy(&msg[0], data, size);

    fd = g_io_channel_unix_get_fd(chan);

    len = write(fd, msg, size);
    if (len < 0) {
        error("BT socket write error: %s (%d)", strerror(errno), errno);
        return false;
    }

    if ((size_t) len < size) {
        error("BT socket write error: partial write (%zd of %zu bytes)",
              len, size);
        return false;
    }

    return true;
}


static bool ih_receive_data_from_remote(GIOChannel *chan, struct input_host *host, bool is_control)
{
    int fd;
    ssize_t len;
    uint8_t hdr;
    uint8_t data[UHID_DATA_MAX + 1];

    fd = g_io_channel_unix_get_fd(chan);

    len = read(fd, data, sizeof(data));
    if (len < 0) {
        error("BT socket read error: %s (%d)", strerror(errno), errno);
        return false;
    }

    if (len == 0) {
        DBG("BT socket read returned 0 bytes");
        return true;
    }

    ih_send_data_to_local(is_control ? host->ctrl_io_local_connection : host->intr_io_local_connection, data, len);

    return true;
}

static gboolean ih_remote_channel_watch_cb(GIOChannel *chan, GIOCondition cond, gpointer data, bool is_control) {
    struct input_host *host = data;
    if (cond == G_IO_IN || cond == G_IO_PRI || cond == (G_IO_IN | G_IO_PRI)) {
        //DBG("%s data is in ", is_control ? "ctrl":"intr");
        ih_receive_data_from_remote(chan, host, is_control);
        return TRUE;
    }
    else if(cond & G_IO_NVAL){
        DBG("%s invalid message", is_control ? "ctrl":"intr");
        return TRUE;
    }
    DBG("Input host %s %s disconnected. Shutting down channels", is_control ? "ctrl":"intr", host->dst_address);
    //ih_shutdown_channels(host);
    ih_shutdown_remote_connections(host);
    return FALSE;
}


gboolean ih_remote_interrupt_watch_cb(GIOChannel *chan, GIOCondition cond, gpointer data) {
    return ih_remote_channel_watch_cb(chan, cond, data, FALSE);
}

gboolean ih_remote_control_watch_cb(GIOChannel *chan, GIOCondition cond, gpointer data) {
    return ih_remote_channel_watch_cb(chan, cond, data, TRUE);
}

void ih_shutdown_remote_connections(struct input_host *host) {
    if (host->ctrl_io_remote_connection_watch > 0)
        g_source_remove(host->ctrl_io_remote_connection_watch);
    host->ctrl_io_remote_connection_watch = 0;

    if (host->intr_io_remote_connection_watch > 0)
        g_source_remove(host->intr_io_remote_connection_watch);
    host->intr_io_remote_connection_watch = 0;

    if (host->intr_io_remote_connection) {
        g_io_channel_shutdown(host->intr_io_remote_connection, TRUE, NULL);
        g_io_channel_unref(host->intr_io_remote_connection);
        host->intr_io_remote_connection = NULL;
    }

    if (host->ctrl_io_remote_connection) {
        g_io_channel_shutdown(host->ctrl_io_remote_connection, TRUE, NULL);
        g_io_channel_unref(host->ctrl_io_remote_connection);
        host->ctrl_io_remote_connection = NULL;
    }
}
