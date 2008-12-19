"""
Offer a 1-1 stream tube and close the connection. Salut must just send a
stanza to close the tube and disconnect.
"""

from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy

from twisted.words.xish import xpath, domish

import dbus

PUBLISHED_NAME="test-tube"

CHANNEL_TYPE_TUBES = "org.freedesktop.Telepathy.Channel.Type.Tubes"
HT_CONTACT = 1
HT_CONTACT_LIST = 3
SOCKET_ADDRESS_TYPE_IPV4 = dbus.UInt32(2)
SOCKET_ACCESS_CONTROL_LOCALHOST = dbus.UInt32(0)

print "FIXME: test-tube-close.py disabled because sending a close stanza on "
print "disconnection is not yet implemented in telepathy-salut. It requires "
print "to ensure the XmppConnection and reestablish it"
# exiting 77 causes automake to consider the test to have been skipped
raise SystemExit(77)

def test(q, bus, conn):
    # Salut will not connect to this socket, the test finishs before
    socket_address = ('0.0.0.0', dbus.UInt16(0))

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    contact_name = PUBLISHED_NAME + "@" + get_host_name()
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

    t = conn.RequestChannel(CHANNEL_TYPE_TUBES, HT_CONTACT, handle,
        True)
    tubes_channel = make_channel_proxy(conn, t, "Channel.Type.Tubes")

    tubes_channel.OfferStreamTube("http", dbus.Dictionary({}),
            SOCKET_ADDRESS_TYPE_IPV4, socket_address,
            SOCKET_ACCESS_CONTROL_LOCALHOST, "")
    
    e = q.expect('stream-iq')

    # Close the connection just after the tube has been offered.
    conn.Disconnect()

    # receive the close stanza for the tube
    event = q.expect('stream-message')
    message = event.stanza
    close_node = xpath.queryForNodes('/message/close[@xmlns="%s"]' % NS_TUBES,
        message)
    assert close_node is not None

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
