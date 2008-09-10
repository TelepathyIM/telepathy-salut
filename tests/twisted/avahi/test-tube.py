from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy
from trivialstream import TrivialStreamServer

from twisted.words.xish import xpath, domish

import time
import dbus

PUBLISHED_NAME="test-tube"

CHANNEL_TYPE_TUBES = "org.freedesktop.Telepathy.Channel.Type.Tubes"
HT_CONTACT = 1
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)
SOCKET_ADDRESS_TYPE_IPV4 = dbus.UInt32(2)
SOCKET_ACCESS_CONTROL_LOCALHOST = dbus.UInt32(0)

INCOMING_MESSAGE = "Test 123"
OUTGOING_MESSAGE = "Test 321"

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

def test(q, bus, conn):
    server = TrivialStreamServer()
    server.run()
    socket_address = server.socket_address
    socket_address = (socket_address[0],
            dbus.UInt16(socket_address[1]))

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

    t = conn.RequestChannel(CHANNEL_TYPE_TUBES, HT_CONTACT, handle,
        True)
    tubes_channel = make_channel_proxy(conn, t, "Channel.Type.Tubes")

    tubes_channel.OfferStreamTube("http", sample_parameters,
            SOCKET_ADDRESS_TYPE_IPV4, socket_address,
            SOCKET_ACCESS_CONTROL_LOCALHOST, "")
    
    e = q.expect('stream-iq')
    iq_tube = xpath.queryForNodes('/iq/tube', e.stanza)[0]
    transport = xpath.queryForNodes('/iq/tube/transport', e.stanza)[0]
    assert iq_tube.attributes['type'] == 'stream'
    assert iq_tube.attributes['service'] == 'http'
    assert iq_tube.attributes['id'] is not None

    params = {}
    parameter_nodes = xpath.queryForNodes('/iq/tube/parameters/parameter',
        e.stanza)
    for node in parameter_nodes:
        assert node['name'] not in params
        params[node['name']] = (node['type'], str(node))
    assert params == {'ay': ('bytes', 'aGVsbG8='),
                      's': ('str', 'hello'),
                      'i': ('int', '-123'),
                      'u': ('uint', '123'),
                     }, params

if __name__ == '__main__':
    exec_test(test)
