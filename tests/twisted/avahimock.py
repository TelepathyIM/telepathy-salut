#!/usr/bin/python

import socket

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

AVAHI_DNS_CLASS_IN = 1
AVAHI_DNS_TYPE_A = 1


def emit_signal(object_path, interface, name, destination, signature, *args):
    message = SignalMessage(object_path, interface, name)
    message.append(*args, signature=signature)

    if destination is not None:
        message.set_destination(destination)

    dbus.SystemBus().send_message(message)


def get_domain():
    full_domain = socket.getfqdn()
    if '.' in full_domain:
        return full_domain.split('.', 1)[1]
    else:
        return ''

class Model(object):
    def __init__(self):
        self._service_browsers = []
        self._service_resolvers = []
        self._entries = []
        self._address_records = {}

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
            address = self._resolve_hostname(entry.host)
            emit_signal(service_resolver.object_path,
                        AVAHI_IFACE_SERVICE_RESOLVER, 'Found',
                        service_resolver.client, 'iissssisqaayu',
                        entry.interface, entry.protocol, entry.name, entry.type,
                        entry.domain, entry.host, entry.aprotocol,
                        address, entry.port, entry.txt, entry.flags)

    def _resolve_hostname(self, hostname):
        if hostname in self._address_records:
            return self._address_records[hostname]
        else:
            return socket.gethostbyname(hostname)

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

    def add_record(self, interface, protocol, flags, name, clazz, type_, ttl,
                   rdata):
        if clazz == AVAHI_DNS_CLASS_IN and type_ == AVAHI_DNS_TYPE_A:
            self._address_records[name] = socket.inet_ntoa(rdata)

    def remove_entry(self, type_, name):
        entry = self._find_entry(type_, name)

        if entry is None:
            raise ValueError('No entry for type %s and name %s' % (type_, name))

        for service_browser in self._service_browsers:
            if service_browser.type == type_:
                self._emit_item_remove(service_browser, entry)

        self._entries.remove(entry)

    def _emit_new_item(self, service_browser, entry):
        emit_signal(service_browser.object_path,
                    AVAHI_IFACE_SERVICE_BROWSER, 'ItemNew',
                    service_browser.client, 'iisssu',
                    entry.interface, entry.protocol, entry.name, entry.type,
                    entry.domain, entry.flags)

    def _emit_item_remove(self, service_browser, entry):
        emit_signal(service_browser.object_path,
                    AVAHI_IFACE_SERVICE_BROWSER, 'ItemRemove',
                    service_browser.client, 'iisssu',
                    entry.interface, entry.protocol, entry.name, entry.type,
                    entry.domain, entry.flags)

    def _emit_found(self, service_resolver, entry):
        address = self._resolve_hostname(entry.host)
        emit_signal(service_resolver.object_path,
                    AVAHI_IFACE_SERVICE_RESOLVER, 'Found',
                    service_resolver.client, 'iissssisqaayu',
                    entry.interface, entry.protocol, entry.name, entry.type,
                    entry.domain, entry.host, entry.aprotocol,
                    address, entry.port, entry.txt, entry.flags)

    def remove_client(self, client):
        for service_browser in self._service_browsers[:]:
            if service_browser.client == client:
                service_browser.Free()
                service_browser.remove_from_connection()
                self._service_browsers.remove(service_browser)

        for service_resolver in self._service_resolvers[:]:
            if service_resolver.client == client:
                service_resolver.Free()
                service_resolver.remove_from_connection()
                self._service_resolvers.remove(service_resolver)


class Entry(object):
    def __init__(self, interface, protocol, flags, name, type_, domain, host,
                 port, txt):
        self.name = name
        self.type = type_

        self.interface = None
        self.protocol = None
        self.aprotocol = None
        self.flags = None
        self.domain = None
        self.host = None
        self.port = None
        self.txt = None

        self.update(interface, protocol, flags, domain, host, port, txt)

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

        bus.add_signal_receiver(self.__name_owner_changed_cb,
                                signal_name='NameOwnerChanged',
                                dbus_interface='org.freedesktop.DBus')

        self._entry_groups = []
        self._model = Model()

    def __name_owner_changed_cb(self, name, old_owner, new_owner):
        if new_owner == '':
            for entry_group in self._entry_groups[:]:
                if entry_group.client == name:
                    entry_group.Free()
                    entry_group.remove_from_connection()
                    self._entry_groups.remove(entry_group)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='u')
    def GetAPIVersion(self):
        return 515

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetHostName(self):
        return socket.gethostname()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetHostNameFqdn(self):
        return socket.getfqdn()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='s')
    def GetDomainName(self):
        return get_domain()

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='i')
    def GetState(self):
        return 2

    @dbus.service.signal(dbus_interface=AVAHI_IFACE_SERVER, signature='is')
    def StateChanged(self, state, error):
        pass

    @dbus.service.method(dbus_interface=AVAHI_IFACE_SERVER,
                         in_signature='', out_signature='o',
                         sender_keyword='sender')
    def EntryGroupNew(self, sender):
        index = len(self._entry_groups) + 1
        entry_group = EntryGroup(sender, index, self._model)
        self._entry_groups.append(entry_group)
        return entry_group.object_path

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


class EntryGroup(dbus.service.Object):
    def __init__(self, client, index, model):
        bus = dbus.SystemBus()
        self.object_path = '/Client%u/EntryGroup%u' % (1, index)
        dbus.service.Object.__init__(self, conn=bus,
                                     object_path=self.object_path)

        self._state = 0
        self.client = client
        self._model = model

        self._entries = []

    def get_service(self, name):
        return self._services.get(name, None)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='iiussssqaay', out_signature='')
    def AddService(self, interface, protocol, flags, name, type_, domain, host,
                   port, txt):
        if not host:
            host = socket.gethostname()

        if not domain:
            domain = get_domain()

        self._model.update_entry(interface, protocol, flags, name, type_, domain,
                                 host, port, txt)
        self._entries.append((type_, name))

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='iiusssaay', out_signature='')
    def UpdateServiceTxt(self, interface, protocol, flags, name, type_, domain, txt):
        self._model.update_entry(interface, protocol, flags, name, type_, domain,
                                 None, None, txt)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='', out_signature='')
    def Commit(self):
        self._set_state(1)
        glib.idle_add(lambda: self._set_state(2))

    def _set_state(self, new_state):
        self._state = new_state

        message = SignalMessage(self.object_path,
                                AVAHI_IFACE_ENTRY_GROUP,
                                'StateChanged')
        message.append(self._state, 'org.freedesktop.Avahi.Success',
                       signature='is')
        message.set_destination(self.client)

        dbus.SystemBus().send_message(message)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='', out_signature='i')
    def GetState(self):
        return self._state

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='iiusqquay', out_signature='',
                         byte_arrays=True)
    def AddRecord(self, interface, protocol, flags, name, clazz, type_, ttl,
                  rdata):
        self._model.add_record(interface, protocol, flags, name, clazz, type_,
                               ttl, rdata)

    @dbus.service.method(dbus_interface=AVAHI_IFACE_ENTRY_GROUP,
                         in_signature='', out_signature='')
    def Free(self):
        for type_, name in self._entries[:]:
            self._model.remove_entry(type_, name)
            self._entries.remove((type_, name))


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
