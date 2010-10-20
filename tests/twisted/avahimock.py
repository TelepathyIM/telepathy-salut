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


def emit_signal(object_path, interface, name, destination, signature, *args):
    message = SignalMessage(object_path, interface, name)
    message.append(*args, signature=signature)

    if destination is not None:
        message.set_destination(destination)

    dbus.SystemBus().send_message(message)


class Model(object):
    def __init__(self):
        self._service_browsers = []
        self._service_resolvers = []
        self._entries = []

    def new_service_browser(self, type_, client):
        index = len(self._service_browsers) + 1
        service_browser = ServiceBrowser(client, index, type_)
        self._service_browsers.append(service_browser)

        glib.idle_add(self.__browse_idle_cb, service_browser)

        return service_browser.object_path

    def __browse_idle_cb(self, service_browser):
        for entry in self._entries:
            if entry.type == service_browser.type:
                self._emit_new_item(service_browser, entry)

    def _find_entry(self, type_, name):
        for entry in self._entries:
            if entry.type == type_ and entry.name == name:
                return entry
        return None

    def new_service_resolver(self, type_, name, client):
        index = len(self._service_resolvers) + 1
        entry = self._find_entry(type_, name)
        service_resolver = ServiceResolver(index, client, type_, name)
        self._service_resolvers.append(service_resolver)

        glib.idle_add(self.__entry_found_idle_cb, service_resolver, entry)

        return service_resolver.object_path

    def __entry_found_idle_cb(self, service_resolver, entry):
        if entry is None:
            emit_signal(service_resolver.object_path,
                        AVAHI_IFACE_SERVICE_RESOLVER, 'Failure',
                        service_resolver.client, 's',
                        'fill with a proper error string')
        else:
            emit_signal(service_resolver.object_path,
                        AVAHI_IFACE_SERVICE_RESOLVER, 'Found',
                        service_resolver.client, 'iissssisqaayu',
                        entry.interface, entry.protocol, entry.name, entry.type,
                        entry.domain, entry.host, entry.aprotocol,
                        entry.address, entry.port, entry.txt, entry.flags)

    def update_entry(self, interface, protocol, flags, name, type_, domain,
                     host, port, txt):

        entry = self._find_entry(type_, name)

        if interface == -1:
            interface = 0

        if protocol == -1:
            protocol = 0

        if host is None:
            host = entry.host

        if port is None:
            port = entry.port

        if entry is None:
            entry = Entry(interface, protocol, flags, name, type_, domain,
                          host, port, txt)
            self._entries.append(entry)
        else:
            entry.update(interface, protocol, flags, domain, host, port, txt)

        for service_browser in self._service_browsers:
            if service_browser.type == type_:
                self._emit_new_item(service_browser, entry)

        for service_resolver in self._service_resolvers:
            if service_resolver.type == type_ and \
                service_resolver.name == name:
                self._emit_found(service_resolver, entry)

    def _emit_new_item(self, service_browser, entry):
        emit_signal(service_browser.object_path,
                    AVAHI_IFACE_SERVICE_BROWSER, 'ItemNew',
                    service_browser.client, 'iisssu',
                    entry.interface, entry.protocol, entry.name, entry.type,
                    entry.domain, entry.flags)

    def _emit_found(self, service_resolver, entry):
        emit_signal(service_resolver.object_path,
                    AVAHI_IFACE_SERVICE_RESOLVER, 'Found',
                    service_resolver.client, 'iissssisqaayu',
                    entry.interface, entry.protocol, entry.name, entry.type,
                    entry.domain, entry.host, entry.aprotocol,
                    entry.address, entry.port, entry.txt, entry.flags)


class Entry(object):
    def __init__(self, interface, protocol, flags, name, type_, domain, host,
                 port, txt):
        self.interface = interface
        self.protocol = protocol
        self.aprotocol = protocol
        self.flags = flags
        self.name = name
        self.type = type_
        self.domain = domain
        self.host = host
        self.address = '192.168.1.1'
        self.port = port
        self.txt = txt

    def update(self, interface, protocol, flags, domain, host, port, txt):
        self.interface = interface
        self.protocol = protocol
        self.aprotocol = protocol
        self.flags = flags
        self.domain = domain
        self.host = host
        self.port = port
        self.txt = txt

class Avahi(dbus.service.Object):
    def __init__(self):
        bus = dbus.SystemBus()
        name = dbus.service.BusName(AVAHI_NAME, bus)
        dbus.service.Object.__init__(self, conn=bus, object_path='/',
                                     bus_name=name)

        self._entry_groups = []
        self._model = Model()

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
        entry_group = EntryGroup(sender, index, self._model)
        self._entry_groups.append(entry_group)
        return entry_group.object_path

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
        return self._model.new_service_browser(type_, sender)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='iisssiu', out_signature='o',
                         sender_keyword='sender')
    def ServiceResolverNew(self, interface, protocol, name, type_, domain, aprotocol, flags, sender):
        return self._model.new_service_resolver(type_, name, sender)

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


class EntryGroup(dbus.service.Object):
    def __init__(self, client, index, model):
        bus = dbus.SystemBus()
        self.object_path = '/Client%u/EntryGroup%u' % (1, index)
        dbus.service.Object.__init__(self, conn=bus,
                                     object_path=self.object_path)

        self._state = 0
        self._client = client
        self._model = model

    def get_service(self, name):
        return self._services.get(name, None)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='iiussssqaay', out_signature='')
    def AddService(self, interface, protocol, flags, name, type_, domain, host,
                   port, txt):
        self._model.update_entry(interface, protocol, flags, name, type_, domain,
                                 host, port, txt)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='iiusssaay', out_signature='')
    def UpdateServiceTxt(self, interface, protocol, flags, name, type_, domain, txt):
        self._model.update_entry(interface, protocol, flags, name, type_, domain,
                                 None, None, txt)

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

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='', out_signature='')
    def Free(self):
        pass


class ServiceBrowser(dbus.service.Object):
    def __init__(self, client, index, type_):
        bus = dbus.SystemBus()
        self.object_path = '/Client%u/ServiceBrowser%u' % (1, index)
        dbus.service.Object.__init__(self, conn=bus,
                                     object_path=self.object_path)

        self.client = client
        self.type = type_

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVICE_BROWSER,
                         in_signature='', out_signature='')
    def Free(self):
        pass


class ServiceResolver(dbus.service.Object):
    def __init__(self, index, client, type_, name):
        bus = dbus.SystemBus()
        self.object_path = '/Client%u/ServiceResolver%u' % (1, index)
        dbus.service.Object.__init__(self, conn=bus,
                                     object_path=self.object_path)
        self.client = client
        self.type = type_
        self.name = name

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVICE_RESOLVER,
                         in_signature='', out_signature='')
    def Free(self):
        pass


avahi = Avahi()

loop = gobject.MainLoop()
loop.run()
