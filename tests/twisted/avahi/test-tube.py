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

test_string = "This string travels on a tube !"

def test(q, bus, conn):

    # define a basic tcp server that echoes what the client says, but with
    # swapcase
    class TrivialServer(Protocol):
        def dataReceived(self, data):
            self.transport.write(string.swapcase(data))
            e = Event('server-data-received', service = self, data = data)
            q.append(e)

    # define a basic tcp client
    class ClientGreeter(Protocol):
        def dataReceived(self, data):
            e = Event('client-data-received', service = self, data = data)
            q.append(e)
    def client_connected_cb(p):
        e = Event('client-connected', transport = p.transport)
        q.append(e)

    # create the server
    factory = Factory()
    factory.protocol = TrivialServer
    server_socket_address = os.getcwd() + '/stream'
    try:
        os.remove(server_socket_address)
    except OSError, e:
        if e.errno != errno.ENOENT:
            raise
    l = reactor.listenUNIX(server_socket_address, factory)

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

    tube_id = tubes_channel.OfferStreamTube("http", sample_parameters,
            SOCKET_ADDRESS_TYPE_UNIX, dbus.ByteArray(server_socket_address),
            SOCKET_ACCESS_CONTROL_LOCALHOST, "")

    e = q.expect('stream-iq')
    iq_tube = xpath.queryForNodes('/iq/tube', e.stanza)[0]
    transport = xpath.queryForNodes('/iq/tube/transport', e.stanza)[0]
    assert iq_tube.attributes['type'] == 'stream'
    assert iq_tube.attributes['service'] == 'http'
    assert iq_tube.attributes['id'] is not None
    port = transport.attributes['port']
    assert port is not None
    port = int(port)
    assert port > 1024
    assert port < 65536

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

    client = ClientCreator(reactor, ClientGreeter)
    client.connectTCP("localhost", port).addCallback(client_connected_cb)

    e = q.expect('client-connected')
    client_transport = e.transport
    client_transport.write(test_string)

    e = q.expect('server-data-received')
    assert e.data == test_string

    e = q.expect('client-data-received')
    assert e.data == string.swapcase(test_string)

    # Close the tube propertly
    tubes_channel.CloseTube(tube_id)
    conn.Disconnect()

if __name__ == '__main__':
    exec_test(test)
