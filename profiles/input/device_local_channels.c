//
// Created by ruundii on 25/07/2020.
//
#include "device_local_channels.h"

static gboolean id_local_control_connect(GIOChannel *io, GIOCondition cond, gpointer user_data);
static gboolean id_local_interrupt_connect(GIOChannel *io, GIOCondition cond, gpointer user_data);
static gboolean id_local_channel_connect(GIOChannel *io, GIOCondition triggered_cond, bool is_control, struct input_device *device);

static gboolean id_local_control_watch_cb(GIOChannel *io, GIOCondition cond, gpointer user_data);
static gboolean id_local_interrupt_watch_cb(GIOChannel *io, GIOCondition cond, gpointer user_data);
static gboolean id_local_channel_watch_cb(GIOChannel *io, GIOCondition cond, gpointer user_data, bool is_control);

static bool id_receive_data_from_local(GIOChannel *chan, struct input_device *device,  bool is_control);


GIOChannel *id_create_local_listening_sockets(struct input_device *device, bool is_control){
    int sock;
    GIOChannel *io;
    struct sockaddr_un addr;

    sock = socket(PF_LOCAL, SOCK_SEQPACKET, 0);
    if (sock < 0) {
        error("Unable to open %s socket for input device %s: %s", is_control ? "Ctrl":"Intr", device->path, strerror(errno));
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, is_control ? device->socket_path_ctrl : device->socket_path_intr, 33);

    unlink(addr.sun_path);
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        error("Failed to bind %s socket for input device %s : %s", is_control ? "Ctrl":"Intr", device->path, strerror(errno));
        close(sock);
        return NULL;
    }

    if (chmod(addr.sun_path, 0666) < 0)
        error("Failed to change mode");


    io = g_io_channel_unix_new(sock);

    g_io_channel_set_close_on_unref(io, TRUE);
    g_io_channel_set_flags(io, G_IO_FLAG_NONBLOCK, NULL);

    if (listen(sock, 1) < 0) {
        error("Failed to listen to %s socket for input device %s: %s", is_control ? "Ctrl":"Intr", device->path, strerror(errno));
        g_io_channel_unref(io);
        return NULL;
    }

    if(is_control){
        device->ctrl_io_local_listener_watch = g_io_add_watch_full(io, G_PRIORITY_DEFAULT_IDLE, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, id_local_control_connect , device, NULL);
        device->ctrl_io_local_listener = io;
    }
    else{
        device->intr_io_local_listener_watch = g_io_add_watch_full(io, G_PRIORITY_DEFAULT_IDLE, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, id_local_interrupt_connect, device, NULL);
        device->intr_io_local_listener = io;
    }
    return io;
}

static gboolean id_local_control_connect(GIOChannel *io, GIOCondition cond, gpointer user_data) {
    struct input_device *device = user_data;
    return id_local_channel_connect(io,cond, TRUE, device);
}

static gboolean id_local_interrupt_connect(GIOChannel *io, GIOCondition cond, gpointer user_data) {
    struct input_device *device = user_data;
    return id_local_channel_connect(io,cond, FALSE, device);
}

static gboolean id_local_channel_connect(GIOChannel *io, GIOCondition triggered_cond, bool is_control, struct input_device *device) {
    /* If the user closed the connection */
    if ((triggered_cond & G_IO_NVAL) || check_nval(io)) {
        id_shutdown_local_connections(device);
        return TRUE;
    }

    GIOChannel *cli_io;
    int srv_sock, cli_sock;

    srv_sock = g_io_channel_unix_get_fd(io);
    if(is_control && device->ctrl_io_local_connection){
        error("local input device ctrl socket exists");
        return TRUE;
    }
    else if(!is_control && device->intr_io_local_connection){
        error("local input device intr socket exists");
        return TRUE;
    }

    cli_sock = accept(srv_sock, NULL, NULL);
    if (cli_sock < 0)
        return TRUE;


    cli_io = g_io_channel_unix_new(cli_sock);

    g_io_channel_set_close_on_unref(cli_io, TRUE);
    g_io_channel_set_flags(cli_io, G_IO_FLAG_NONBLOCK, NULL);

    GIOCondition cond = G_IO_IN | G_IO_PRI | G_IO_HUP | G_IO_ERR | G_IO_NVAL;

    if(is_control){
        device->ctrl_io_local_connection = cli_io;
        device->ctrl_io_local_connection_watch = g_io_add_watch(device->ctrl_io_local_connection, cond,
                                                              id_local_control_watch_cb, device);
        DBG("local input device ctrl channel connected");
    }
    else{
        device->intr_io_local_connection = cli_io;
        device->intr_io_local_connection_watch = g_io_add_watch(device->intr_io_local_connection, cond,
                                                              id_local_interrupt_watch_cb, device);
        DBG("local input device intr channel connected");
    }
    return TRUE;
}

static gboolean id_local_control_watch_cb(GIOChannel *io, GIOCondition cond, gpointer user_data) {
    return id_local_channel_watch_cb(io,cond,user_data,TRUE);
}

static gboolean id_local_interrupt_watch_cb(GIOChannel *io, GIOCondition cond, gpointer user_data) {
    return id_local_channel_watch_cb(io,cond,user_data,FALSE);
}

static gboolean id_local_channel_watch_cb(GIOChannel *io, GIOCondition cond, gpointer user_data, bool is_control) {
    struct input_device *device = user_data;
    if (cond == G_IO_IN || cond == G_IO_PRI || cond == (G_IO_IN | G_IO_PRI)) {
        DBG("local %s data is in for device", is_control ? "ctrl":"intr");
        id_receive_data_from_local(io, device, is_control);
        return TRUE;
    }
    if(cond & G_IO_NVAL){
        DBG("local %s invalid message for device", is_control ? "ctrl":"intr");
        return TRUE;
    }
    //local channel disconnected - shut down local connections
    DBG("local %s channel disconnected - shutting down local connections for device", is_control ? "ctrl":"intr");
    id_shutdown_local_connections(device);
    return FALSE;
}

static bool id_receive_data_from_local(GIOChannel *chan, struct input_device *device,  bool is_control)
{
    int fd;
    ssize_t len;
    uint8_t data[UHID_DATA_MAX + 1];

    fd = g_io_channel_unix_get_fd(chan);

    len = read(fd, data, sizeof(data));
    if (len < 0) {
        error("local socket read error: %s (%d)", strerror(errno), errno);
        return FALSE;
    }

    if (len == 0) {
        DBG("local socket read returned 0 bytes");
        return TRUE;
    }

    //send data  remote
    hidp_send_message(is_control ? device->ctrl_io : device->intr_io, data[0], data+1, len-1);
    return TRUE;
}

bool id_send_data_to_local(GIOChannel *chan, const uint8_t *data, size_t size)
{
    int fd;
    ssize_t len;
    uint8_t msg[size];

    if (!chan) {
        error("local socket not connected for device");
        return false;
    }

    if (data == NULL)
        size = 0;

    if (size > 0)
        memcpy(&msg[0], data, size);

    fd = g_io_channel_unix_get_fd(chan);

    len = write(fd, msg, size);
    if (len < 0) {
        error("local socket for device write error: %s (%d)", strerror(errno), errno);
        return false;
    }

    if ((size_t) len < size) {
        error("local socket for device write error: partial write (%zd of %zu bytes)",
              len, size);
        return false;
    }

    return true;
}


void id_shutdown_local_connections(struct input_device *device){
    if (device->intr_io_local_connection) {
        g_io_channel_shutdown(device->intr_io_local_connection, TRUE, NULL);
        g_io_channel_unref(device->intr_io_local_connection);
        device->intr_io_local_connection = NULL;
    }

    if (device->intr_io_local_connection_watch > 0)
        g_source_remove(device->intr_io_local_connection_watch);
    device->intr_io_local_connection_watch = 0;


    if (device->ctrl_io_local_connection) {
        g_io_channel_shutdown(device->ctrl_io_local_connection, TRUE, NULL);
        g_io_channel_unref(device->ctrl_io_local_connection);
        device->ctrl_io_local_connection = NULL;
    }

    if (device->ctrl_io_local_connection_watch > 0)
        g_source_remove(device->ctrl_io_local_connection_watch);
    device->ctrl_io_local_connection_watch = 0;
}

void id_shutdown_local_listeners(struct input_device *device){
    if (device->intr_io_local_listener) {
        g_io_channel_shutdown(device->intr_io_local_listener, TRUE, NULL);
        g_io_channel_unref(device->intr_io_local_listener);
        device->intr_io_local_listener = NULL;
    }

    if (device->intr_io_local_listener_watch > 0)
        g_source_remove(device->intr_io_local_listener_watch);
    device->intr_io_local_listener_watch = 0;

    if (device->ctrl_io_local_listener) {
        g_io_channel_shutdown(device->ctrl_io_local_listener, TRUE, NULL);
        g_io_channel_unref(device->ctrl_io_local_listener);
        device->ctrl_io_local_listener = NULL;
    }

    if (device->ctrl_io_local_listener_watch > 0)
        g_source_remove(device->ctrl_io_local_listener_watch);
    device->ctrl_io_local_listener_watch = 0;
}
