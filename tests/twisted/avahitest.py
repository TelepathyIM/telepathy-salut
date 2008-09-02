"""
Infrastructure for testing avahi
"""

import servicetest
from servicetest import Event
import dbus
import dbus.glib
import avahi

def get_server():
  bus = dbus.SystemBus()
  server = dbus.Interface(bus.get_object(avahi.DBUS_NAME,
            avahi.DBUS_PATH_SERVER), avahi.DBUS_INTERFACE_SERVER)
  return server

def get_host_name():
  return get_server().GetHostName()

def get_host_name_fqdn():
  return get_server().GetHostNameFqdn()

def get_domain_name():
  return get_server().GetDomainName()

def txt_get_key(txt, key):
  for x in txt:
      if dbus.Byte('=') in x:
        (rkey, value) = avahi.byte_array_to_string(x).split('=')
        if rkey == key:
            return value

  return None

class AvahiService:
    def __init__(self, event_queue, bus, server,
        interface, protocol, name, type, domain, aprotocol, flags):

        self.event_queue = event_queue

        resolver_path = server.ServiceResolverNew(interface,
            protocol, name, type, domain, aprotocol, flags)
        resolver_obj = bus.get_object(avahi.DBUS_NAME, resolver_path)
        resolver = dbus.Interface(resolver_obj,
            avahi.DBUS_INTERFACE_SERVICE_RESOLVER)

        resolver.connect_to_signal('Found', self._service_found)
        resolver.connect_to_signal('Failed', self._service_failed)

    def _service_found(self, interface, protocol, name, stype, domain,
        host_name, aprotocol, pt, port, txt, flags):

        e = Event('service-resolved', service = self,
          interface = interface, protocol = protocol, name = name,
          stype = stype, host_name = host_name, aprotocol = aprotocol,
          pt = pt, port = port, txt = txt, flags = flags)

        self.event_queue.append(e)

    def _service_failed(self, error):
      e = Event('service-failed', service = self, error = error)
      self.event_queue.append(e)

class AvahiListener:
    def __init__(self, event_queue):
        self.event_queue = event_queue

        self.bus = dbus.SystemBus()
        self.server = dbus.Interface(self.bus.get_object(avahi.DBUS_NAME,
            avahi.DBUS_PATH_SERVER), avahi.DBUS_INTERFACE_SERVER)
        self.browsers = []

    def _service_added_cb(self, interface, protocol, name, stype, domain,
        flags):

        e = Event ('service-added',
          interface=interface, protocol=protocol, name=name, stype=stype,
          domain=domain, flags=flags)
        self.event_queue.append(e)

    def _service_removed_cb(self, interface, protocol, name, stype, domain,
        flags):

        e = Event ('service-removed',
          interface=interface, protocol=protocol, name=name, stype=stype,
          domain=domain, flags=flags)
        self.event_queue.append(e)

    def listen_for_service(self, sname):
        browser_path = self.server.ServiceBrowserNew(avahi.IF_UNSPEC,
            avahi.PROTO_UNSPEC, sname, "local", dbus.UInt32(0));
        browser_obj = self.bus.get_object(avahi.DBUS_NAME, browser_path)
        browser = dbus.Interface(browser_obj,
            avahi.DBUS_INTERFACE_SERVICE_BROWSER)

        browser.connect_to_signal('ItemNew', self._service_added_cb)
        browser.connect_to_signal('ItemRemoved', self._service_removed_cb)

        self.browsers.append(browser)

    def resolver_for_service(self, event):
        return AvahiService(self.event_queue, self.bus, self.server,
          event.interface, event.protocol, event.name, event.stype,
          event.domain, event.protocol, 0)

class AvahiAnnouncer:
    def __init__(self, name, type, port, txt):
        self.name = name
        self.type = type
        self.port = port
        self.txt = txt

        self.bus = dbus.SystemBus()
        self.server = dbus.Interface(self.bus.get_object(avahi.DBUS_NAME,
            avahi.DBUS_PATH_SERVER), avahi.DBUS_INTERFACE_SERVER)

        entry_path = self.server.EntryGroupNew()
        entry_obj = self.bus.get_object(avahi.DBUS_NAME, entry_path)
        entry = dbus.Interface(entry_obj,
            avahi.DBUS_INTERFACE_ENTRY_GROUP)

        entry.AddService(avahi.IF_UNSPEC, avahi.PROTO_UNSPEC,
            dbus.UInt32(0), name, type, get_domain_name(), get_host_name_fqdn(),
            port, avahi.dict_to_txt_array(txt))
        entry.Commit()

        self.entry = entry

    def update(self, txt):
      self.txt.update(txt)

      self.entry.UpdateServiceTxt(avahi.IF_UNSPEC, avahi.PROTO_UNSPEC,
        dbus.UInt32(0), self.name, self.type, get_domain_name(), 
        avahi.dict_to_txt_array(self.txt))


if __name__ == '__main__':
    from twisted.internet import reactor

    txtdict = { "test0": "0", "test1": "1" }

    a = AvahiAnnouncer("test", "_test._tcp", 1234, txtdict)

    q = servicetest.IteratingEventQueue()
    # Set verboseness if needed for debugging
    # q.verbose = True

    l = AvahiListener(q)
    l.listen_for_service("_test._tcp")

    while True:
      e = q.expect ('service-added', stype='_test._tcp')
      # Only care about services we announced ourselves
      if e.flags & (avahi.LOOKUP_RESULT_LOCAL|avahi.LOOKUP_RESULT_OUR_OWN):
          break

    assert "test" == e.name[0:len("test")]

    s = l.resolver_for_service(e)
    e = q.expect('service-resolved', service = s)
    for (key, val ) in txtdict.iteritems():
        v = txt_get_key(e.txt, key)
        assert v == val, (key, val, v)

    txtdict["test1"] = "2"
    txtdict["test2"] = "2"

    a.update(txtdict)

    e = q.expect('service-resolved', service = s)
    for (key, val ) in txtdict.iteritems():
        v = txt_get_key(e.txt, key)
        assert v == val, (key, val, v)
