from saluttest import exec_test, make_connection
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi
import dbus
import os
import errno
import string

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, Event, EventPattern, call_async

from twisted.words.xish import xpath, domish
from twisted.internet.protocol import Factory, Protocol, ClientCreator
from twisted.internet import reactor
from constants import *

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

    # first connection: connect
    contact1_name = "testsuite" + "@" + get_host_name()
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # second connection: connect
    conn2_params = {
        'published-name': 'testsuite2',
        'first-name': 'test2',
        'last-name': 'suite2',
        }
    contact2_name = "testsuite2" + "@" + get_host_name()
    conn2 = make_connection(bus, q.append, conn2_params)
    conn2.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # first connection: get the contact list
    publish_handle = conn.RequestHandles(HT_CONTACT_LIST, ["publish"])[0]
    conn1_publish = conn.RequestChannel(CHANNEL_TYPE_CONTACT_LIST,
        HT_CONTACT_LIST, publish_handle, False)
    conn1_publish_proxy = bus.get_object(conn.bus_name, conn1_publish)

    # second connection: get the contact list
    publish_handle = conn2.RequestHandles(HT_CONTACT_LIST, ["publish"])[0]
    conn2_publish = conn2.RequestChannel(CHANNEL_TYPE_CONTACT_LIST,
        HT_CONTACT_LIST, publish_handle, False)
    conn2_publish_proxy = bus.get_object(conn2.bus_name, conn2_publish)

    # first connection: wait to see contact2
    # The signal MembersChanged may be already emitted... check the Members
    # property first
    contact2_handle_on_conn1 = 0
    conn1_members = conn1_publish_proxy.Get(CHANNEL_IFACE_GROUP, 'Members',
            dbus_interface=PROPERTIES_IFACE)
    for h in conn1_members:
        name = conn.InspectHandles(HT_CONTACT, [h])[0]
        if name == contact2_name:
            contact2_handle_on_conn1 = h
    while contact2_handle_on_conn1 == 0:
        e = q.expect('dbus-signal', signal='MembersChanged', path=conn1_publish)
        for h in e.args[1]:
            name = conn.InspectHandles(HT_CONTACT, [h])[0]
            if name == contact2_name:
                contact2_handle_on_conn1 = h

    # second connection: wait to see contact1
    # The signal MembersChanged may be already emitted... check the Members
    # property first
    contact1_handle_on_conn2 = 0
    conn2_members = conn2_publish_proxy.Get(CHANNEL_IFACE_GROUP, 'Members',
            dbus_interface=PROPERTIES_IFACE)
    for h in conn2_members:
        name = conn.InspectHandles(HT_CONTACT, [h])[0]
        if name == contact1_name:
            contact1_handle_on_conn2 = h
    while contact1_handle_on_conn2 == 0:
        e = q.expect('dbus-signal', signal='MembersChanged', path=conn2_publish)
        for h in e.args[1]:
            name = conn2.InspectHandles(HT_CONTACT, [h])[0]
            if name == contact1_name:
                contact1_handle_on_conn2 = h

    # contact1 offers stream tube to contact2 (old API)
    contact1_tubes_channel_path = conn.RequestChannel(CHANNEL_TYPE_TUBES, HT_CONTACT,
            contact2_handle_on_conn1, True)
    contact1_tubes_channel = make_channel_proxy(conn, contact1_tubes_channel_path, "Channel.Type.Tubes")

    tube_id = contact1_tubes_channel.OfferStreamTube("http", sample_parameters,
            SOCKET_ADDRESS_TYPE_UNIX, dbus.ByteArray(server_socket_address),
            SOCKET_ACCESS_CONTROL_LOCALHOST, "")

    contact2_tubes_channel_path = None
    while contact2_tubes_channel_path is None:
        e = q.expect('dbus-signal', signal='NewChannel')
        if (e.args[1] == CHANNEL_TYPE_TUBES) and (e.path.endswith("testsuite2") == True):
            contact2_tubes_channel_path = e.args[0]

    contact2_tubes_channel = make_channel_proxy(conn2, contact2_tubes_channel_path, "Channel.Type.Tubes")

    contact2_tubes = contact2_tubes_channel.ListTubes()
    assert len(contact2_tubes) == 1
    contact2_tube = contact2_tubes[0]
    assert contact2_tube[0] is not None # tube id
    assert contact2_tube[1] is not None # initiator
    assert contact2_tube[2] == 1 # type = stream tube
    assert contact2_tube[3] == 'http' # service = http
    assert contact2_tube[4] is not None # parameters
    assert contact2_tube[5] == 0, contact2_tube[5] # status = local pending

    unix_socket_adr = contact2_tubes_channel.AcceptStreamTube(
            contact2_tube[0], 0, 0, '', byte_arrays=True)

    client = ClientCreator(reactor, ClientGreeter)
    client.connectUNIX(unix_socket_adr).addCallback(client_connected_cb)

    e = q.expect('client-connected')
    client_transport = e.transport
    client_transport.write(test_string)

    e = q.expect('server-data-received')
    assert e.data == test_string

    e = q.expect('client-data-received')
    assert e.data == string.swapcase(test_string)

    # Close the tube propertly
    call_async(q, contact1_tubes_channel, 'CloseTube', tube_id)

    q.expect_many(
        EventPattern('dbus-signal', signal='TubeClosed', path=contact1_tubes_channel_path),
        EventPattern('dbus-signal', signal='TubeClosed', path=contact2_tubes_channel_path),
        EventPattern('dbus-return', method='CloseTube'))

    conn.Disconnect()
    conn2.Disconnect()

if __name__ == '__main__':
    exec_test(test)
