from dasbus.connection import SystemMessageBus
import dasbus.typing as dt
import sys
from dasbus.server.interface import dbus_interface
#from dasbus.loop import EventLoop
from agent import Agent
import socket
import threading

import asyncio, gbulb
gbulb.install()
#import asyncio_glib
#asyncio.set_event_loop_policy(asyncio_glib.GLibEventLoopPolicy())

# UUID for HID service (1124)
# https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
UUID = '00001124-0000-1000-8000-00805f9b34fb'
ADAPTER_OBJECT = '/org/bluez/hci0'
DEVICE_INTERFACE = 'org.bluez.Device1'
INPUT_DEVICE_INTERFACE = 'org.bluez.Input1'
INPUT_HOST_INTERFACE = 'org.bluez.InputHost1'
DEVICE_NAME = 'Bluetooth HID Hub - Ubuntu'

bus = SystemMessageBus()
loop = asyncio.get_event_loop()


class BluetoothAdapter:
    def __init__(self):
        self.om = bus.get_proxy(service_name= "org.bluez", object_path="/", interface_name="org.freedesktop.DBus.ObjectManager")
        self.om.InterfacesAdded.connect(self.interfaces_added)
        self.om.InterfacesRemoved.connect(self.interfaces_removed)
        objs = self.om.GetManagedObjects()
        self.host_sockets={}
        self.device_sockets={}
        if ADAPTER_OBJECT in objs:
            print("Adapter ",ADAPTER_OBJECT, " found")
            self.init_adapter()
        else:
            print("Adapter ",ADAPTER_OBJECT, " not found. Please make sure you have Bluetooth Adapter installed and plugged in")
            self.adapter = None

        for obj in list(objs):
            if INPUT_HOST_INTERFACE in objs[obj]:
                socket_path_ctrl = objs[obj][INPUT_HOST_INTERFACE]["SocketPathCtrl"]
                socket_path_intr = objs[obj][INPUT_HOST_INTERFACE]["SocketPathIntr"]
                if not isinstance(socket_path_ctrl, str):
                    socket_path_ctrl = socket_path_ctrl.unpack()
                    socket_path_intr = socket_path_intr.unpack()
                asyncio.ensure_future(self.open_sockets(True, obj, socket_path_ctrl, socket_path_intr))
            elif INPUT_DEVICE_INTERFACE in objs[obj]:
                socket_path_ctrl = objs[obj][INPUT_DEVICE_INTERFACE]["SocketPathCtrl"]
                socket_path_intr = objs[obj][INPUT_DEVICE_INTERFACE]["SocketPathIntr"]
                if not isinstance(socket_path_ctrl, str):
                    socket_path_ctrl = socket_path_ctrl.unpack()
                    socket_path_intr = socket_path_intr.unpack()
                asyncio.ensure_future(self.open_sockets(False, obj, socket_path_ctrl, socket_path_intr))

    def init_adapter(self):
        self.adapter = bus.get_proxy(service_name="org.bluez", object_path=ADAPTER_OBJECT,
                                     interface_name="org.bluez.Adapter1")

        if self.adapter is None:
            print("Adapter not found")
            return
        if not self.powered:
            print("Bluetooth adapter is turned off. Trying to turn on")
            try:
                self.powered = True
                if(self.powered):
                    print("Successfully turned on")
                else:
                    print("Failed to turn on. Please turn on Bluetooth in the system")
                    return

            except Exception:
                print("Failed to turn on. Please turn on Bluetooth in the system")
                return
        self.alias = DEVICE_NAME
        self.discoverable = True
        self.discoverable_timeout = 0

    async def open_sockets(self, is_host, obj, ctrl, intr):
        dev = bus.get_proxy(service_name="org.bluez", object_path=obj, interface_name=DEVICE_INTERFACE)
        record = {"name":dev.Name, "alias":dev.Alias, "ctrl":ctrl, "intr":intr}
        s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        s.connect(ctrl)
        s.setblocking(False)
        record["ctrl_conn"] = s
        s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        s.connect(intr)
        s.setblocking(False)
        record["intr_conn"] = s
        if is_host:
            self.host_sockets[obj] = record
        else:
            self.device_sockets[obj] = record

        asyncio.ensure_future(self.loop_of_fun(obj, is_host, True))
        asyncio.ensure_future(self.loop_of_fun(obj, is_host, False))

    async def loop_of_fun(self, obj, is_host, is_ctrl):
        records = self.host_sockets if is_host else self.device_sockets
        records_other_side = self.device_sockets if is_host else self.host_sockets
        key = "ctrl_conn" if is_ctrl else "intr_conn"
        while obj in records:
            msg = await loop.sock_recv(records[obj][key],255)
            for counterpart in list(records_other_side):
                if(key in records_other_side[counterpart]):
                    await loop.sock_sendall(records_other_side[counterpart][key], msg)

    def close_sockets(self, is_host,  obj):
        record = self.host_sockets[obj] if is_host else self.device_sockets[obj]

        if("ctrl_conn" in record):
            record["ctrl_conn"].close()
        if("intr__conn" in record):
            record["intr__conn"].close()
        if is_host:
            del self.host_sockets[obj]
        else:
            del self.device_sockets[obj]

    @property
    def powered(self):
        return self.adapter.Powered

    @powered.setter
    def powered(self, new_value):
        self.adapter.Powered = new_value

    @property
    def alias(self):
        return self.adapter.Alias

    @alias.setter
    def alias(self, new_value):
        self.adapter.Alias = new_value

    @property
    def discoverable(self):
        return self.adapter.Discoverable

    @discoverable.setter
    def discoverable(self, new_value):
        self.adapter.Discoverable = new_value

    @property
    def discoverable_timeout(self):
        return self.adapter.DiscoverableTimeout

    @discoverable_timeout.setter
    def discoverable_timeout(self, new_value):
        self.adapter.DiscoverableTimeout = new_value


    def interfaces_added(self, obj_name, interfaces):
        if(obj_name==ADAPTER_OBJECT):
            print("Bluetooth adapter added. Starting")
            self.init_adapter()
        elif INPUT_HOST_INTERFACE in interfaces:
            ih = bus.get_proxy(service_name="org.bluez", object_path=obj_name,
                                    interface_name=INPUT_HOST_INTERFACE)
            self.open_sockets(True, obj_name, ih.SocketPathCtrl, ih.SocketPathIntr)
        elif INPUT_DEVICE_INTERFACE in interfaces:
            id = bus.get_proxy(service_name="org.bluez", object_path=obj_name,
                                    interface_name=INPUT_DEVICE_INTERFACE)
            self.open_sockets(False, obj_name, id.SocketPathCtrl, id.SocketPathIntr)

    def interfaces_removed(self, obj_name, interfaces):
        if(obj_name==ADAPTER_OBJECT):
            self.adapter = None
            print("Bluetooth adapter removed. Stopping")
        elif INPUT_HOST_INTERFACE in interfaces:
            self.close_sockets(True, obj_name)
        elif INPUT_DEVICE_INTERFACE in interfaces:
            self.close_sockets(False, obj_name)


if __name__ == "__main__":
    agent = Agent()
    bus.publish_object(DBUS_PATH_AGENT,agent)
    agent_manager = bus.get_proxy(service_name= "org.bluez", object_path="/org/bluez", interface_name="org.bluez.AgentManager1")
    agent_manager.RegisterAgent(DBUS_PATH_AGENT, "KeyboardOnly")
    agent_manager.RequestDefaultAgent(DBUS_PATH_AGENT)
    print("Agent registered")

    a = BluetoothAdapter()
    loop.run_forever()


#print(proxy)