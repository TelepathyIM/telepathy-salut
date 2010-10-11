#!/usr/bin/python

import dbus
import dbus.service
import gobject

from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

AVAHI_NAME = 'org.freedesktop.Avahi'
AVAHI_IFACE_SERVER = 'org.freedesktop.Avahi.Server'

class Avahi(dbus.service.Object):
    def __init__(self):
        bus = dbus.SystemBus()
        name = dbus.service.BusName(AVAHI_NAME, bus)
        dbus.service.Object.__init__(self, conn=bus, object_path='/',
                                     bus_name=name)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetVersionString(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='u')
    def GetAPIVersion(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetHostName(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='s', out_signature='')
    def SetHostName(self, name):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetHostNameFqdn(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetDomainName(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='b')
    def IsNSSSupportAvailable(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='i')
    def GetState(self):
        raise NotImplementedError()

    @dbus.service.signal(dbus_interface=AVAHI_IFACE_SERVER, signature='is')
    def StateChanged(self, state, error):
        pass

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='u')
    def GetLocalServiceCookie(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='s', out_signature='s')
    def GetAlternativeHostName(self, name):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='s', out_signature='s')
    def GetAlternativeServiceName(self, name):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='i', out_signature='s')
    def GetNetworkInterfaceNameByIndex(self, index):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='s', out_signature='i')
    def GetNetworkInterfaceIndexByName(self, name):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisiu', out_signature='iisisu')
    def ResolveHostName(self, interface, protocol, name, aprotocol, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisu', out_signature='iiissu')
    def ResolveAddress(self, interface, protocol, address, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisssiu', out_signature='iissssisqaayu')
    def ResolveService(self, interface, protocol, name, type_, domain, aprotocol,
                       flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='o')
    def EntryGroupNew(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisiu', out_signature='o')
    def DomainBrowserNew(self, interface, protocol, domain, btype, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisu', out_signature='o')
    def ServiceTypeBrowserNew(self, interface, protocol, domain, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iissu', out_signature='o')
    def ServiceBrowserNew(self, interface, protocol, type_, domain, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisssiu', out_signature='o')
    def ServiceResolverNew(self, interface, protocol, name, type_, domain, aprotocol, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisiu', out_signature='o')
    def HostNameResolverNew(self, interface, protocol, name, aprotocol, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisu', out_signature='o')
    def AddressResolverNew(self, interface, protocol, address, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisqqu', out_signature='o')
    def RecordBrowserNew(self, interface, protocol, name, clazz, type_, flags):
        raise NotImplementedError()

avahi = Avahi()

loop = gobject.MainLoop()
loop.run()
