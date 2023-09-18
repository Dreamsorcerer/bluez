//
// Created by ruundii on 25/07/2020.
//

#include "host_local_channels.h"
static gboolean ih_local_control_watch_cb(GIOChannel *io, GIOCondition cond,
                                       gpointer user_data);

static gboolean ih_local_interrupt_watch_cb(GIOChannel *io, GIOCondition cond,
                                         gpointer user_data);

static gboolean ih_local_channel_watch_cb(GIOChannel *io, GIOCondition cond,
                                       gpointer user_data, bool is_control);

static bool ih_receive_data_from_local(GIOChannel *chan, struct input_host *host,  bool is_control);

static gboolean ih_local_control_connect(GIOChannel *io, GIOCondition cond, gpointer user_data);
static gboolean ih_local_interrupt_connect(GIOChannel *io, GIOCondition cond, gpointer user_data);
static gboolean ih_local_channel_connect(GIOChannel *io, GIOCondition triggered_cond, bool is_control, struct input_host *host);


GIOChannel *ih_create_local_listening_sockets(struct input_host *host, bool is_control){
    int sock;
    GIOChannel *io;
    struct sockaddr_un addr;

    sock = socket(PF_LOCAL, SOCK_SEQPACKET, 0);
    if (sock < 0) {
        error("Unable to open %s socket for input host %s: %s", is_control ? "Ctrl":"Intr", host->path, strerror(errno));
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, is_control ? host->socket_path_ctrl : host->socket_path_intr, 33);

    unlink(addr.sun_path);
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        error("Failed to bind %s socket for input host %s : %s", is_control ? "Ctrl":"Intr", host->path, strerror(errno));
        close(sock);
        return NULL;
    }

    if (chmod(addr.sun_path, 0666) < 0)
        error("Failed to change mode");


    io = g_io_channel_unix_new(sock);

    g_io_channel_set_close_on_unref(io, TRUE);
    g_io_channel_set_flags(io, G_IO_FLAG_NONBLOCK, NULL);

    if (listen(sock, 1) < 0) {
        error("Failed to listen to %s socket for input host %s: %s", is_control ? "Ctrl":"Intr", host->path, strerror(errno));
        g_io_channel_unref(io);
        return NULL;
    }

    if(is_control){
        host->ctrl_io_local_listener_watch = g_io_add_watch_full(io, G_PRIORITY_DEFAULT_IDLE, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, ih_local_control_connect , host, NULL);
        host->ctrl_io_local_listener = io;
    }
    else{
        host->intr_io_local_listener_watch = g_io_add_watch_full(io, G_PRIORITY_DEFAULT_IDLE, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, ih_local_interrupt_connect, host, NULL);
        host->intr_io_local_listener = io;
    }
    return io;
}


static gboolean ih_local_control_connect(GIOChannel *io, GIOCondition cond,
                                   gpointer user_data) {
    struct input_host *host = user_data;
    return ih_local_channel_connect(io,cond, TRUE, host);
}

static gboolean ih_local_interrupt_connect(GIOChannel *io, GIOCondition cond,
                                   gpointer user_data) {
    struct input_host *host = user_data;
    return ih_local_channel_connect(io,cond, FALSE, host);
}

static gboolean ih_local_channel_connect(GIOChannel *io, GIOCondition triggered_cond, bool is_control, struct input_host *host) {
    /* If the user closed the connection */
    if ((triggered_cond & G_IO_NVAL) || check_nval(io)) {
        ih_shutdown_local_connections(host);
        return TRUE;
    }

    GIOChannel *cli_io;
    int srv_sock, cli_sock;

    srv_sock = g_io_channel_unix_get_fd(io);
    if(is_control && host->ctrl_io_local_connection){
        error("local input host ctrl socket exists");
        return TRUE;
    }
    else if(!is_control && host->intr_io_local_connection){
        error("local input host intr socket exists");
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
        host->ctrl_io_local_connection = cli_io;
        host->ctrl_io_local_connection_watch = g_io_add_watch(host->ctrl_io_local_connection, cond,
                                                              ih_local_control_watch_cb, host);
        DBG("local input host ctrl channel connected");
    }
    else{
        host->intr_io_local_connection = cli_io;
        host->intr_io_local_connection_watch = g_io_add_watch(host->intr_io_local_connection, cond,
                                                              ih_local_interrupt_watch_cb, host);
        DBG("local input host intr channel connected");
    }
    return TRUE;
}

static gboolean ih_local_control_watch_cb(GIOChannel *io, GIOCondition cond,
                                       gpointer user_data) {
    return ih_local_channel_watch_cb(io,cond,user_data,TRUE);
}

static gboolean ih_local_interrupt_watch_cb(GIOChannel *io, GIOCondition cond,
                                         gpointer user_data) {
    return ih_local_channel_watch_cb(io,cond,user_data,FALSE);
}

static gboolean ih_local_channel_watch_cb(GIOChannel *io, GIOCondition cond,
                               gpointer user_data, bool is_control) {
    struct input_host *host = user_data;
    if (cond == G_IO_IN || cond == G_IO_PRI || cond == (G_IO_IN | G_IO_PRI)) {
        //DBG("local %s data is in for host", is_control ? "ctrl":"intr");
        ih_receive_data_from_local(io, host, is_control);
        return TRUE;
    }
    if(cond & G_IO_NVAL){
        DBG("local %s invalid message for host", is_control ? "ctrl":"intr");
        return TRUE;
    }
    //local channel disconnected - shut down local connections
    DBG("local %s channel disconnected - shutting down local connections for host", is_control ? "ctrl":"intr");
    ih_shutdown_local_connections(host);
    return FALSE;
}


static bool ih_receive_data_from_local(GIOChannel *chan, struct input_host *host,  bool is_control)
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

    GIOChannel *rchan = is_control ? host->ctrl_io_remote_connection : host->intr_io_remote_connection;
    if (!rchan) {
        DBG("BT socket not connected. Trying to re-connect with input host");
        input_host_reconnect(host);
        return TRUE;
    }
    //send data  remote
    ih_send_data_to_remote(rchan, data, len);
    return TRUE;
}

bool ih_send_data_to_local(GIOChannel *chan, const uint8_t *data, size_t size)
{
    int fd;
    ssize_t len;
    uint8_t msg[size];

    if (!chan) {
        error("local socket not connected for host");
        return false;
    }

    if (data == NULL)
        size = 0;

    if (size > 0)
        memcpy(&msg[0], data, size);

    fd = g_io_channel_unix_get_fd(chan);

    len = write(fd, msg, size);
    if (len < 0) {
        error("local socket for host write error: %s (%d)", strerror(errno), errno);
        return false;
    }

    if ((size_t) len < size) {
        error("local socket for host write error: partial write (%zd of %zu bytes)",
              len, size);
        return false;
    }

    return true;
}

void ih_shutdown_local_connections(struct input_host *host) {
    if (host->intr_io_local_connection) {
        g_io_channel_shutdown(host->intr_io_local_connection, TRUE, NULL);
        g_io_channel_unref(host->intr_io_local_connection);
        host->intr_io_local_connection = NULL;
    }

    if (host->intr_io_local_connection_watch > 0)
        g_source_remove(host->intr_io_local_connection_watch);
    host->intr_io_local_connection_watch = 0;


    if (host->ctrl_io_local_connection) {
        g_io_channel_shutdown(host->ctrl_io_local_connection, TRUE, NULL);
        g_io_channel_unref(host->ctrl_io_local_connection);
        host->ctrl_io_local_connection = NULL;
    }

    if (host->ctrl_io_local_connection_watch > 0)
        g_source_remove(host->ctrl_io_local_connection_watch);
    host->ctrl_io_local_connection_watch = 0;
}

void ih_shutdown_local_listeners(struct input_host *host) {
    if (host->intr_io_local_listener) {
        g_io_channel_shutdown(host->intr_io_local_listener, TRUE, NULL);
        g_io_channel_unref(host->intr_io_local_listener);
        host->intr_io_local_listener = NULL;
    }

    if (host->intr_io_local_listener_watch > 0)
        g_source_remove(host->intr_io_local_listener_watch);
    host->intr_io_local_listener_watch = 0;

    if (host->ctrl_io_local_listener) {
        g_io_channel_shutdown(host->ctrl_io_local_listener, TRUE, NULL);
        g_io_channel_unref(host->ctrl_io_local_listener);
        host->ctrl_io_local_listener = NULL;
    }

    if (host->ctrl_io_local_listener_watch > 0)
        g_source_remove(host->ctrl_io_local_listener_watch);
    host->ctrl_io_local_listener_watch = 0;
}
