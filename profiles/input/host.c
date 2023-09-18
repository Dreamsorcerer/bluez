//
// Created by ruundii on 20/07/2020.
//

#include "host.h"
#include "host_local_channels.h"
#include "host_remote_channels.h"


#define INPUT_HOST_INTERFACE "org.bluez.InputHost1"

static void ih_remote_control_reconnect_cb(GIOChannel *chan, GError *conn_err, gpointer user_data);
static void ih_remote_interrupt_reconnect_cb(GIOChannel *chan, GError *conn_err, gpointer user_data);

static GSList* hosts = NULL;

static gboolean property_get_socket_path_ctrl(const GDBusPropertyTable *property, DBusMessageIter *iter, void *data)
{
    struct input_host *host = data;
    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &host->socket_path_ctrl);
    return TRUE;
}

static gboolean property_get_socket_path_intr(const GDBusPropertyTable *property, DBusMessageIter *iter, void *data)
{
    struct input_host *host = data;
    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &host->socket_path_intr);
    return TRUE;
}


static const GDBusPropertyTable input_properties[] = {
        { "SocketPathCtrl", "s", property_get_socket_path_ctrl },
        { "SocketPathIntr", "s", property_get_socket_path_intr },
        { }
};


static void register_socket_and_dbus_interface(struct input_host *host){
    if(ih_create_local_listening_sockets(host, TRUE) == NULL){
        error("Unable to register ctrl socket %s interface", INPUT_HOST_INTERFACE);
        return;
    }
    if(ih_create_local_listening_sockets(host, FALSE) == NULL){
        error("Unable to register intr socket %s interface", INPUT_HOST_INTERFACE);
        return;
    }

    if(!host->dbus_interface_registered) {
        if (g_dbus_register_interface(btd_get_dbus_connection(),
                                      host->path, INPUT_HOST_INTERFACE,
                                      NULL, NULL,
                                      input_properties, host,
                                      NULL) == FALSE) {
            error("Unable to register %s interface", INPUT_HOST_INTERFACE);
        }
        else host->dbus_interface_registered=true;
    }
}

static int cmp_host_addr(gconstpointer a, gconstpointer dst)
{
    const struct input_host *host = a;
    return bacmp(&host->dst, dst);
}

static gboolean device_property_state_changed(DBusConnection *conn, DBusMessage *msg,
                                               void *user_data) {
    struct input_host *host = user_data;
    if (host->device == NULL) {
        ih_shutdown_channels(host);
    }
    else {
        bool connected = btd_device_is_connected(host->device);
        if (!connected) {
            ih_shutdown_remote_connections(host);
            //ih_shutdown_channels(host);
        }
    }
    return TRUE;
}


static struct input_host *create_input_host(const bdaddr_t *src, const bdaddr_t *dst){
    //create a new host
    struct btd_device *device = btd_adapter_find_device(adapter_find(src), dst, BDADDR_BREDR);
    const char *path = device_get_path(device);
    struct input_host *new_host = g_new0(struct input_host, 1);
    new_host->path = g_strdup(path);
    new_host->dbus_interface_registered = false;
    bacpy(&new_host->src, src);
    bacpy(&new_host->dst, dst);
    ba2str(&new_host->dst, new_host->dst_address);
    new_host->device = btd_device_ref(device);
    new_host->socket_path_ctrl = g_strjoin(NULL,"/tmp/BTIHS_", new_host->dst_address, "_Ctrl", NULL);
    new_host->socket_path_intr = g_strjoin(NULL,"/tmp/BTIHS_", new_host->dst_address, "_Intr", NULL);
    hosts= g_slist_append(hosts, new_host);


    g_dbus_add_properties_watch(btd_get_dbus_connection(), NULL,
                            new_host->path, DEVICE_INTERFACE,
                            device_property_state_changed, new_host,
                            NULL);

    return new_host;
}

static struct input_host *find_host(const bdaddr_t *src, const bdaddr_t *dst, bool create_if_not_found){
    GSList *host_list_item = g_slist_find_custom(hosts, dst, cmp_host_addr);
    struct btd_device *device = btd_adapter_find_device(adapter_find(src), dst, BDADDR_BREDR);

    if (device == NULL)
        return NULL;

    if (host_list_item == NULL) {
        if(!create_if_not_found) return NULL;
        return create_input_host(src, dst);
    }

    return host_list_item->data;
}



int input_host_set_channel(const bdaddr_t *src, const bdaddr_t *dst, int psm, GIOChannel *io) {
    struct input_host *host = find_host(src, dst, TRUE);
    if (host == NULL)
        return -ENOENT;

    GIOCondition cond = G_IO_IN | G_IO_PRI | G_IO_HUP | G_IO_ERR | G_IO_NVAL;

    switch (psm) {
        case L2CAP_PSM_HIDP_CTRL:
            if (host->ctrl_io_remote_connection)
                return -EALREADY;
            host->ctrl_io_remote_connection = g_io_channel_ref(io);
            host->ctrl_io_remote_connection_watch = g_io_add_watch(host->ctrl_io_remote_connection, cond,
                                                                   ih_remote_control_watch_cb, host);
            break;
        case L2CAP_PSM_HIDP_INTR:
            if (host->intr_io_remote_connection)
                return -EALREADY;
            host->intr_io_remote_connection = g_io_channel_ref(io);
            host->intr_io_remote_connection_watch = g_io_add_watch(host->intr_io_remote_connection, cond,
                                                                   ih_remote_interrupt_watch_cb, host);

            GError *gerr = NULL;
            if (!bt_io_set(host->intr_io_remote_connection, &gerr,
                           BT_IO_OPT_SEC_LEVEL, BT_IO_SEC_MEDIUM,
                           BT_IO_OPT_INVALID)) {
                error("cannot set encryption required by keyboard: %s", gerr->message);
                g_error_free(gerr);
                return -EFAULT;
            }

            break;
    }

    if(host->ctrl_io_remote_connection!= NULL && host->intr_io_remote_connection!=NULL){
        register_socket_and_dbus_interface(host);
    }
    return 0;
}

int input_host_reconnect(struct input_host *host)
{
    GError *err = NULL;
    GIOChannel *io;

    /* Make sure the device is bonded if required */
    if (!device_is_bonded(host->device, btd_device_get_bdaddr_type(host->device)))
        return -EIO;

        /* If the device is temporary we are not required to reconnect
     * with the device. This is likely the case of a removing device.
     */
    if (device_is_temporary(host->device) ||
        btd_device_is_connected(host->device) || host->ctrl_io_remote_connection)
        return -EALREADY;


    if (g_get_real_time() < host->reconnect_attempt_start + 20000000) // 20 sec throttle
        return -EALREADY; //already reconnecting

    host->reconnect_attempt_start = g_get_real_time();

    io = bt_io_connect(ih_remote_control_reconnect_cb, host,
                       NULL, &err,
                       BT_IO_OPT_SOURCE_BDADDR, &host->src,
                       BT_IO_OPT_DEST_BDADDR, &host->dst,
                       BT_IO_OPT_PSM, L2CAP_PSM_HIDP_CTRL,
                       BT_IO_OPT_SEC_LEVEL, BT_IO_SEC_MEDIUM,
                       BT_IO_OPT_INVALID);
    host->ctrl_io_remote_connection = io;

    if (err == NULL)
        return 0;

    error("%s", err->message);
    g_error_free(err);
    return -EIO;
}

static void ih_remote_control_reconnect_cb(GIOChannel *chan, GError *conn_err, gpointer user_data)
{
    struct input_host *host = user_data;
    GIOCondition cond = G_IO_IN | G_IO_PRI | G_IO_HUP | G_IO_ERR | G_IO_NVAL;
    GIOChannel *io;
    GError *err = NULL;

    if (conn_err) {
        error("%s", conn_err->message);
        ih_shutdown_remote_connections(host);
        return;
    }

    /* Connect to the HID interrupt channel */
    io = bt_io_connect(ih_remote_interrupt_reconnect_cb, host,
                       NULL, &err,
                       BT_IO_OPT_SOURCE_BDADDR, &host->src,
                       BT_IO_OPT_DEST_BDADDR, &host->dst,
                       BT_IO_OPT_PSM, L2CAP_PSM_HIDP_INTR,
                       BT_IO_OPT_SEC_LEVEL, BT_IO_SEC_MEDIUM,
                       BT_IO_OPT_INVALID);
    if (!io) {
        error("%s", err->message);
        g_error_free(err);
        ih_shutdown_remote_connections(host);
        return;
    }

    host->intr_io_remote_connection = io;

    host->ctrl_io_remote_connection_watch = g_io_add_watch(host->ctrl_io_remote_connection, cond, ih_remote_control_watch_cb, host);
}

static void ih_remote_interrupt_reconnect_cb(GIOChannel *chan, GError *conn_err, gpointer user_data)
{
    struct input_host *host = user_data;
    GIOCondition cond = G_IO_IN | G_IO_PRI | G_IO_HUP | G_IO_ERR | G_IO_NVAL;
    int err;

    if (conn_err) {
        err = -EIO;
        ih_shutdown_remote_connections(host);
        return;
    }

    if (host->intr_io_remote_connection == NULL || host->ctrl_io_remote_connection == NULL) {
        ih_shutdown_remote_connections(host);
        return;
    }

    host->intr_io_remote_connection_watch = g_io_add_watch(host->intr_io_remote_connection, cond,
                                                           ih_remote_interrupt_watch_cb, host);
}


void ih_shutdown_channels(struct input_host *host){
    if(host->dbus_interface_registered) {
        g_dbus_unregister_interface(btd_get_dbus_connection(),
                                    host->path, INPUT_HOST_INTERFACE);
        host->dbus_interface_registered = false;
    }
    ih_shutdown_remote_connections(host);
    ih_shutdown_local_connections(host);
    ih_shutdown_local_listeners(host);
}

//since we are not getting device remove notification, remove host when connection attempt unsuccessful
int input_host_remove(const bdaddr_t *src, const bdaddr_t *dst)
{
    struct input_host *host = find_host(src, dst, FALSE);
    if (host == NULL)
        return -ENOENT;
    DBG("Removing input host %s", host->dst_address);
    ih_shutdown_channels(host);

    //btd_adapter_remove_device(adapter_find(&host->src), host->device);
    btd_device_unref(host->device);
    g_free(host->path);
    if(host->socket_path_ctrl != NULL) g_free(host->socket_path_ctrl);
    if(host->socket_path_intr!=NULL) g_free(host->socket_path_intr);

    hosts = g_slist_remove(hosts, host);
    g_free(host);
    return 0;
}

//TODO: host removal on channel disconnect or device removal ?
