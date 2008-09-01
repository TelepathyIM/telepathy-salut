"""
Infrastructure for testing avahi
"""

import servicetest
from servicetest import Event
import dbus
import dbus.glib
import avahi

def get_host_name():
  bus = dbus.SystemBus()
  server = dbus.Interface(bus.get_object(avahi.DBUS_NAME,
            avahi.DBUS_PATH_SERVER), avahi.DBUS_INTERFACE_SERVER)
  return server.GetHostName()

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
