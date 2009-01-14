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
from config import PACKAGE_STRING

from twisted.words.xish import xpath, domish

import time
import dbus
import hashlib
import base64

HT_CONTACT = 1
caps_iface = 'org.freedesktop.Telepathy.' + \
             'Connection.Interface.ContactCapabilities.DRAFT'
NS_TELEPATHY_CAPS = 'http://telepathy.freedesktop.org/caps'

def check_caps(txt, ver=None):
    for (key, val) in { "1st": "test",
                        "last": "suite",
                        "status": "avail",
                        "txtvers": "1" }.iteritems():
        v =  txt_get_key(txt, key)
        assert v == val, (key, val, v)

    if ver is None:
        assert txt_get_key(txt, "hash") is None
        assert txt_get_key(txt, "node") is None
        assert txt_get_key(txt, "ver") is None
    else:
        assert txt_get_key(txt, "hash") == "sha-1"
        assert txt_get_key(txt, "node") == NS_TELEPATHY_CAPS

        v = txt_get_key(txt, "ver")
        assert v == ver, (v, ver)

def compute_caps_hash(identities, features, dataforms):
    S = ''

    for identity in sorted(identities):
        S += '%s<' % identity

    for feature in sorted(features):
        S += '%s<' % feature

    # FIXME: support dataforms

    m = hashlib.sha1()
    m.update(S)
    return base64.b64encode(m.digest())

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
    check_caps(e.txt)

    conn_caps_iface = dbus.Interface(conn, caps_iface)

    # Advertise nothing
    conn_caps_iface.SetSelfCapabilities([])

    e = q.expect('service-resolved', service = service)

    caps = compute_caps_hash(['client/pc//%s' % PACKAGE_STRING], [], [])
    check_caps(e.txt, ver=caps)

if __name__ == '__main__':
    # example from XEP-0115
    assert compute_caps_hash(['client/pc//Exodus 0.9.1'], ["http://jabber.org/protocol/disco#info",
        "http://jabber.org/protocol/disco#items", "http://jabber.org/protocol/muc",
        "http://jabber.org/protocol/caps"], []) == 'QgayPKawpkPSDYmwT/WM94uAlu0='
    exec_test(test)
