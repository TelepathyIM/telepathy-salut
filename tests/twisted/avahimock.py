#!/usr/bin/python

import dbus
import dbus.service
from dbus.lowlevel import SignalMessage
import gobject
import glib

from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

AVAHI_NAME = 'org.freedesktop.Avahi'
AVAHI_IFACE_SERVER = 'org.freedesktop.Avahi.Server'
AVAHI_IFACE_ENTRY_GROUP = 'org.freedesktop.Avahi.EntryGroup'
AVAHI_IFACE_SERVICE_BROWSER = 'org.freedesktop.Avahi.ServiceBrowser'
AVAHI_IFACE_SERVICE_RESOLVER = 'org.freedesktop.Avahi.ServiceResolver'

class Avahi(dbus.service.Object):
    def __init__(self):
        bus = dbus.SystemBus()
        name = dbus.service.BusName(AVAHI_NAME, bus)
        dbus.service.Object.__init__(self, conn=bus, object_path='/',
                                     bus_name=name)

        self._entry_groups = []
        self._service_browsers = {}
        self._service_resolvers = []

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetVersionString(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='u')
    def GetAPIVersion(self):
        return 515

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetHostName(self):
        return 'avahimock_hostname'

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='s', out_signature='')
    def SetHostName(self, name):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetHostNameFqdn(self):
        return 'avahimock_hostname.local'

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetDomainName(self):
        return 'local'

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='b')
    def IsNSSSupportAvailable(self):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='i')
    def GetState(self):
        return 2

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
                         in_signature='', out_signature='o',
                         sender_keyword='sender')
    def EntryGroupNew(self, sender):
        index = len(self._entry_groups) + 1
        entry_group = EntryGroup(sender, index, self._item_new_cb)
        self._entry_groups.append(entry_group)
        return entry_group.object_path

    def _item_new_cb(self, interface, protocol, flags, name, type_, domain):
        if type_ not in self._service_browsers:
            return

        self._service_browsers[type_].new_item(interface, protocol, flags, name,
                                               type_, domain)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisiu', out_signature='o')
    def DomainBrowserNew(self, interface, protocol, domain, btype, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisu', out_signature='o')
    def ServiceTypeBrowserNew(self, interface, protocol, domain, flags):
        raise NotImplementedError()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iissu', out_signature='o',
                         sender_keyword='sender')
    def ServiceBrowserNew(self, interface, protocol, type_, domain, flags, sender):
        index = len(self._service_browsers) + 1
        service_browser = ServiceBrowser(sender, index)
        self._service_browsers[type_] = service_browser

        return service_browser.object_path

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisssiu', out_signature='o',
                         sender_keyword='sender')
    def ServiceResolverNew(self, interface, protocol, name, type_, domain, aprotocol, flags, sender):
        index = len(self._service_resolvers) + 1

        service = None
        for entry_group in self._entry_groups:
            service = entry_group.get_service(name)
            if service is not None:
                break

        assert service is not None

        parameters = (interface, protocol, name, type_, domain, service.host,
                      aprotocol, '192.168.0.1', service.port, service.txt,
                      flags)

        service_resolver = ServiceResolver(sender, index, parameters)
        self._service_resolvers.append(service_resolver)

        return service_resolver.object_path

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


class Service(object):
    def __init__(self, interface, protocol, flags, name, type_, domain, host,
                 port, txt):
        self.interface = interface
        self.protocol = protocol
        self.flags = flags
        self.name = name
        self.type = type_
        self.domain = domain
        self.host = host
        self.port = port
        self.txt = txt

class EntryGroup(dbus.service.Object):
    def __init__(self, client, index, item_new_cb):
        bus = dbus.SystemBus()
        self.object_path = '/Client%u/EntryGroup%u' % (1, index)
        dbus.service.Object.__init__(self, conn=bus,
                                     object_path=self.object_path)

        self._item_new_cb = item_new_cb
        self._state = 0
        self._client = client
        self._services = {}

    def get_service(self, name):
        return self._services.get(name, None)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='iiussssqaay', out_signature='')
    def AddService(self, interface, protocol, flags, name, type_, domain, host,
                   port, txt):
        self._services[name] = Service(interface, protocol, flags, name, type_,
                                       domain, host, port, txt)
        self._item_new_cb(interface, protocol, flags, name, type_, domain)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='iiusssaay', out_signature='')
    def UpdateServiceTxt(self, interface, protocol, flags, name, type_, domain, txt):
        self._item_new_cb(interface, protocol, flags, name, type_, domain)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='', out_signature='')
    def Commit(self):
        self._set_state(1)
        glib.timeout_add(1000, lambda: self._set_state(2))

    def _set_state(self, new_state):
        self._state = new_state

        message = SignalMessage(self.object_path,
                                AVAHI_IFACE_ENTRY_GROUP,
                                'StateChanged')
        message.append(self._state, 'org.freedesktop.Avahi.Success',
                       signature='is')
        message.set_destination(self._client)

        dbus.SystemBus().send_message(message)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='', out_signature='i')
    def GetState(self):
        return self._state


class ServiceBrowser(dbus.service.Object):
    def __init__(self, client, index):
        bus = dbus.SystemBus()
        self.object_path = '/Client%u/ServiceBrowser%u' % (1, index)
        dbus.service.Object.__init__(self, conn=bus,
                                     object_path=self.object_path)

        self._client = client

    def new_item(self, interface, protocol, flags, name, type_, domain):
        message = SignalMessage(self.object_path,
                                AVAHI_IFACE_SERVICE_BROWSER,
                                'ItemNew')
        message.append(interface, protocol, name, type_, domain, flags,
                       signature='iisssu')
        message.set_destination(self._client)

        dbus.SystemBus().send_message(message)

class ServiceResolver(dbus.service.Object):
    def __init__(self, client, index, parameters):
        bus = dbus.SystemBus()
        self.object_path = '/Client%u/ServiceResolver%u' % (1, index)
        dbus.service.Object.__init__(self, conn=bus,
                                     object_path=self.object_path)

        self._client = client
        self._parameters = parameters

        glib.idle_add(self._idle_cb)

    def _idle_cb(self):
        message = SignalMessage(self.object_path,
                                AVAHI_IFACE_SERVICE_RESOLVER,
                                'Found')
        message.append(*self._parameters, signature='iissssisqaayu')
        message.set_destination(self._client)

        dbus.SystemBus().send_message(message)


avahi = Avahi()

loop = gobject.MainLoop()
loop.run()
