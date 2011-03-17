"""
Basic test of SetSelfCapabilities on interface
org.freedesktop.Telepathy.Connection.Interface.ContactCapabilities.DRAFT
"""

from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
from avahitest import txt_get_key
import avahi

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy
from saluttest import fixed_features

from twisted.words.xish import xpath, domish
from caps_helper import compute_caps_hash, check_caps
from config import PACKAGE_STRING
import ns
import constants as cs

import time
import dbus

HT_CONTACT = 1

def test(q, bus, conn):
    # last value of the "ver" key we resolved. We use it to be sure that the
    # modified caps has already be announced.
    old_ver = None

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

    ver = txt_get_key(e.txt, "ver")
    while ver == old_ver:
        # be sure that the announced caps actually changes
        e = q.expect('service-resolved', service=service)
        ver = txt_get_key(e.txt, "ver")
    old_ver = ver

    # We support OOB file transfer
    caps = compute_caps_hash(['client/pc//%s' % PACKAGE_STRING],
        fixed_features, [])
    check_caps(e.txt, caps)

    conn_caps_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACT_CAPS)

    # Advertise nothing
    conn_caps_iface.UpdateCapabilities([])

    service.resolve()
    e = q.expect('service-resolved', service = service)

    # Announced capa didn't change
    caps = compute_caps_hash(['client/pc//%s' % PACKAGE_STRING],
        fixed_features, [])

    check_caps(e.txt, caps)

if __name__ == '__main__':
    exec_test(test)
