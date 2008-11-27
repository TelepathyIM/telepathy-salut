"""
Test if 1-1 tubes support is properly disabled.
This test should be removed as soon as we re-enable 1-1 tubes support.
"""
from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi
import dbus
import os
import errno
import string

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, Event

from twisted.words.xish import xpath, domish
from twisted.internet.protocol import Factory, Protocol, ClientCreator
from twisted.internet import reactor

PUBLISHED_NAME="test-tube"

CHANNEL_TYPE_TUBES = "org.freedesktop.Telepathy.Channel.Type.Tubes"
HT_CONTACT = 1
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)
SOCKET_ADDRESS_TYPE_UNIX = dbus.UInt32(0)
SOCKET_ADDRESS_TYPE_IPV4 = dbus.UInt32(2)
SOCKET_ACCESS_CONTROL_LOCALHOST = dbus.UInt32(0)

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    contact_name = PUBLISHED_NAME + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)

    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port, basic_txt)

    publish_handle = conn.RequestHandles(HT_CONTACT_LIST, ["publish"])[0]
    publish = conn.RequestChannel(
        "org.freedesktop.Telepathy.Channel.Type.ContactList",
        HT_CONTACT_LIST, publish_handle, False)

    handle = 0
    # Wait until the record shows up in publish
    while handle == 0:
        e = q.expect('dbus-signal', signal='MembersChanged', path=publish)
        for h in e.args[1]:
            name = conn.InspectHandles(HT_CONTACT, [h])[0]
            if name == contact_name:
                handle = h

    # we can't request 1-1 tubes channel
    try:
        conn.RequestChannel(CHANNEL_TYPE_TUBES, HT_CONTACT, handle,
            True)
    except dbus.DBusException, e:
        assert e.get_dbus_name() == 'org.freedesktop.Telepathy.Errors.NotAvailable'
    else:
        assert False

if __name__ == '__main__':
    exec_test(test)
