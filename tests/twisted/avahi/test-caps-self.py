"""
Basic test of SetSelfCapabilities on interface
org.freedesktop.Telepathy.Connection.Interface.ContactCapabilities.DRAFT
"""

from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy

from twisted.words.xish import xpath, domish

import time
import dbus

HT_CONTACT = 1
caps_iface = 'org.freedesktop.Telepathy.' + \
             'Connection.Interface.ContactCapabilities.DRAFT'

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(HT_CONTACT, [self_handle])[0]

    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    e = q.expect('service-resolved', service = service)

    conn_caps_iface = dbus.Interface(conn, caps_iface)

    # Advertise nothing
    conn_caps_iface.SetSelfCapabilities([])

if __name__ == '__main__':
    exec_test(test)
