from saluttest import exec_test
import dbus
import os
import errno
import string

from servicetest import make_channel_proxy, Event, EventPattern, call_async

from twisted.internet.protocol import Factory, Protocol, ClientCreator
from twisted.internet import reactor
from constants import *
import tubetestutil as t

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

test_string = "This string travels on a tube !"

SERVER_WELCOME_MSG = "Welcome!"

print "FIXME: disabled because 1-1 tubes are disabled for now"
# exiting 77 causes automake to consider the test to have been skipped
raise SystemExit(77)

def test(q, bus, conn):

    # define a basic tcp server that echoes what the client says, but with
    # swapcase
    class TrivialServer(Protocol):
        def dataReceived(self, data):
            self.transport.write(string.swapcase(data))
            e = Event('server-data-received', service = self, data = data)
            q.append(e)

        def connectionMade(self):
            e = Event('server-connected', transport = self.transport)
            q.append(e)

            # send welcome message to the client
            self.transport.write(SERVER_WELCOME_MSG)

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

    contact1_name, conn2, contact2_name, contact2_handle_on_conn1,\
        contact1_handle_on_conn2 = t.connect_two_accounts(q, bus, conn)

    conn1_self_handle = conn.GetSelfHandle()

    # contact1 offers stream tube to contact2 (old API)
    contact1_tubes_channel_path = conn.RequestChannel(CHANNEL_TYPE_TUBES, HT_CONTACT,
            contact2_handle_on_conn1, True)
    contact1_tubes_channel = make_channel_proxy(conn, contact1_tubes_channel_path, "Channel.Type.Tubes")
    contact1_tubes_channel_iface = make_channel_proxy(conn, contact1_tubes_channel_path, "Channel")

    tube_id = contact1_tubes_channel.OfferStreamTube("http", sample_parameters,
            SOCKET_ADDRESS_TYPE_UNIX, dbus.ByteArray(server_socket_address),
            SOCKET_ACCESS_CONTROL_LOCALHOST, "")

    contact2_tubes_channel_path = None
    while contact2_tubes_channel_path is None:
        e = q.expect('dbus-signal', signal='NewChannel')
        if (e.args[1] == CHANNEL_TYPE_TUBES) and (e.path.endswith("testsuite2") == True):
            contact2_tubes_channel_path = e.args[0]

    contact2_tubes_channel = make_channel_proxy(conn2, contact2_tubes_channel_path, "Channel.Type.Tubes")
    contact2_tubes_channel_iface = make_channel_proxy(conn, contact2_tubes_channel_path, "Channel")

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

    # server got the connection
    _, e, new_conn_event, data_event = q.expect_many(
        EventPattern('server-connected'),
        EventPattern('client-connected'),
        EventPattern('dbus-signal', signal='StreamTubeNewConnection', path=contact1_tubes_channel_path),
        EventPattern('client-data-received'))

    client_transport = e.transport

    id, handle = new_conn_event.args
    assert id == tube_id
    assert handle == contact2_handle_on_conn1

    # client receives server's welcome message
    assert data_event.data == SERVER_WELCOME_MSG

    client_transport.write(test_string)

    server_received, client_received = q.expect_many(
        EventPattern('server-data-received'),
        EventPattern('client-data-received'))

    assert server_received.data == test_string
    assert client_received.data == string.swapcase(test_string)

    # Close the tube propertly
    call_async(q, contact1_tubes_channel, 'CloseTube', tube_id)

    q.expect_many(
        EventPattern('dbus-signal', signal='TubeClosed', path=contact1_tubes_channel_path),
        EventPattern('dbus-signal', signal='TubeClosed', path=contact2_tubes_channel_path),
        EventPattern('dbus-return', method='CloseTube'))

    # close both tubes channels
    contact1_tubes_channel_iface.Close()
    contact2_tubes_channel_iface.Close()

    # now contact1 will offer another stream tube to contact2 using the new
    # API

    # Can we request private stream tubes?
    properties = conn.GetAll(CONN_IFACE_REQUESTS, dbus_interface=PROPERTIES_IFACE)

    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_CONTACT},
         [TARGET_HANDLE, TARGET_ID, STREAM_TUBE_SERVICE]
        ) in properties.get('RequestableChannelClasses'),\
                 properties['RequestableChannelClasses']

    requestotron = dbus.Interface(conn, CONN_IFACE_REQUESTS)

    requestotron.CreateChannel({
        CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
        TARGET_HANDLE_TYPE: HT_CONTACT,
        TARGET_ID: contact2_name,
        STREAM_TUBE_SERVICE: 'test'})

    # tubes and tube channels are created on the first connection
    e = q.expect('dbus-signal', signal='NewChannels', path=conn.object.object_path)
    channels = e.args[0]
    assert len(channels) == 2

    # get the list of all channels to check that newly announced ones are in it
    all_channels = conn.Get(CONN_IFACE_REQUESTS, 'Channels', dbus_interface=PROPERTIES_IFACE,
        byte_arrays=True)

    got_tubes, got_tube = False, False
    for path, props in channels:
        if props[CHANNEL_TYPE] == CHANNEL_TYPE_TUBES:
            got_tubes = True
            assert props[REQUESTED] == False
            assert props[INTERFACES] == []
        elif props[CHANNEL_TYPE] == CHANNEL_TYPE_STREAM_TUBE:
            got_tube = True
            assert props[REQUESTED] == True
            assert props[INTERFACES] == [CHANNEL_IFACE_TUBE]
            assert props[STREAM_TUBE_SERVICE] == 'test'

            contact1_tube = bus.get_object(conn.bus_name, path)
            contact1_stream_tube = make_channel_proxy(conn, path, "Channel.Type.StreamTube")
            contact1_tube_channel = make_channel_proxy(conn, path, "Channel")
            tube1_path = path
        else:
            assert False

        assert props[INITIATOR_HANDLE] == conn1_self_handle
        assert props[INITIATOR_ID] == contact1_name
        assert props[TARGET_ID] == contact2_name

        assert (path, props) in all_channels, (path, props)

    assert got_tubes
    assert got_tube

    state = contact1_stream_tube.Get(CHANNEL_IFACE_TUBE, 'State', dbus_interface=PROPERTIES_IFACE)
    assert state == TUBE_CHANNEL_STATE_NOT_OFFERED

    call_async(q, contact1_stream_tube, 'Offer', SOCKET_ADDRESS_TYPE_UNIX,
            dbus.ByteArray(server_socket_address), SOCKET_ACCESS_CONTROL_LOCALHOST, sample_parameters)

    _, return_event, new_chans = q.expect_many(
        EventPattern('dbus-signal', signal='TubeChannelStateChanged',
            args=[TUBE_CHANNEL_STATE_REMOTE_PENDING]),
        EventPattern('dbus-return', method='Offer'),
        EventPattern('dbus-signal', signal='NewChannels', path=conn2.object.object_path))

    state = contact1_stream_tube.Get(CHANNEL_IFACE_TUBE, 'State', dbus_interface=PROPERTIES_IFACE)
    assert state == TUBE_CHANNEL_STATE_REMOTE_PENDING

    # tube and tubes channels have been created on conn2
    channels = new_chans.args[0]
    assert len(channels) == 2

    # get the list of all channels to check that newly announced ones are in it
    all_channels = conn2.Get(CONN_IFACE_REQUESTS, 'Channels', dbus_interface=PROPERTIES_IFACE,
        byte_arrays=True)

    got_tubes, got_tube = False, False
    for path, props in channels:
        if props[CHANNEL_TYPE] == CHANNEL_TYPE_TUBES:
            got_tubes = True
            assert props[REQUESTED] == False
            assert props[INTERFACES] == []
        elif props[CHANNEL_TYPE] == CHANNEL_TYPE_STREAM_TUBE:
            got_tube = True
            assert props[REQUESTED] == False
            assert props[INTERFACES] == [CHANNEL_IFACE_TUBE]
            assert props[STREAM_TUBE_SERVICE] == 'test'

            contact2_tube = bus.get_object(conn.bus_name, path)
            contact2_stream_tube = make_channel_proxy(conn, path, "Channel.Type.StreamTube")
            contact2_tube_channel = make_channel_proxy(conn, path, "Channel")
            tube2_path = path
        else:
            assert False

        assert props[INITIATOR_HANDLE] == contact1_handle_on_conn2
        assert props[INITIATOR_ID] == contact1_name
        assert props[TARGET_ID] == contact1_name

        assert (path, props) in all_channels, (path, props)

    assert got_tubes
    assert got_tube

    state = contact2_tube.Get(CHANNEL_IFACE_TUBE, 'State', dbus_interface=PROPERTIES_IFACE)
    assert state == TUBE_CHANNEL_STATE_LOCAL_PENDING

    # second connection: accept the tube (new API)
    unix_socket_adr = contact2_stream_tube.Accept(SOCKET_ADDRESS_TYPE_UNIX,
        SOCKET_ACCESS_CONTROL_LOCALHOST, '', byte_arrays=True)

    state = contact2_tube.Get(CHANNEL_IFACE_TUBE, 'State', dbus_interface=PROPERTIES_IFACE)
    assert state == TUBE_CHANNEL_STATE_OPEN

    q.expect_many(
        EventPattern('dbus-signal', signal='TubeChannelStateChanged', path=tube2_path,
            args=[TUBE_CHANNEL_STATE_OPEN]),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged', path=tube1_path,
            args=[TUBE_CHANNEL_STATE_OPEN]))

    client = ClientCreator(reactor, ClientGreeter)
    client.connectUNIX(unix_socket_adr).addCallback(client_connected_cb)

    # server got the connection
    _, client_connected, remote_sig, local_sig, data_received = q.expect_many(
        EventPattern('server-connected'),
        EventPattern('client-connected'),
        EventPattern('dbus-signal', signal='NewRemoteConnection',
            path=tube1_path),
        EventPattern('dbus-signal', signal='NewLocalConnection',
            path=tube2_path),
        EventPattern('client-data-received'))

    client_transport = client_connected.transport

    handle, conn_param, contact1_tube_conn_id = remote_sig.args
    assert handle == contact2_handle_on_conn1
    assert contact1_tube_conn_id != 0

    contact2_tube_conn_id = local_sig.args[0]
    assert contact2_tube_conn_id != 0

    # client receives server's welcome message
    assert data_received.data == SERVER_WELCOME_MSG

    client_transport.write(test_string)

    server_received, client_received = q.expect_many(
        EventPattern('server-data-received'),
        EventPattern('client-data-received'))

    assert server_received.data == test_string
    assert client_received.data == string.swapcase(test_string)

    # contact1 close the tube
    call_async(q, contact1_tube_channel, 'Close')

    # tube is closed on both sides
    _, e1, e2, _, _, _, _ = q.expect_many(
        EventPattern('dbus-return', method='Close'),
        EventPattern('dbus-signal', signal='ConnectionClosed', path=tube1_path),
        EventPattern('dbus-signal', signal='ConnectionClosed', path=tube2_path),
        EventPattern('dbus-signal', signal='Closed', path=tube1_path),
        EventPattern('dbus-signal', signal='Closed', path=tube2_path),
        EventPattern('dbus-signal', signal='ChannelClosed', path=conn.object.object_path),
        EventPattern('dbus-signal', signal='ChannelClosed', path=conn2.object.object_path))

    conn_id, error, dbus_msg = e1.args
    assert conn_id == contact1_tube_conn_id
    assert error == CANCELLED

    conn_id, error, dbus_msg = e2.args
    assert conn_id == contact2_tube_conn_id
    assert error == CANCELLED

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
    conn2.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
