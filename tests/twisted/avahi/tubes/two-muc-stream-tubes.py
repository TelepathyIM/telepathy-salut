from saluttest import exec_test
import dbus
import os
import errno
import string
import tempfile

from servicetest import wrap_channel, Event, call_async, EventPattern

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

muc_name = "test-two-muc-stream-tubes"

SERVER_WELCOME_MSG = "Welcome!"

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

    def listen_for_connections():
        server_socket_address = tempfile.mkstemp()[1]
        try:
            os.remove(server_socket_address)
        except OSError, e:
            if e.errno != errno.ENOENT:
                raise
        reactor.listenUNIX(server_socket_address, factory)
        return server_socket_address

    server_socket_address = listen_for_connections()

    contact1_name, conn2, contact2_name, contact2_handle_on_conn1,\
        contact1_handle_on_conn2 = t.connect_two_accounts(q, bus, conn)

    conn1_self_handle = conn.GetSelfHandle()

    # request a stream tube channel (new API)
    conn.Requests.CreateChannel({
        CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
        TARGET_HANDLE_TYPE: HT_ROOM,
        TARGET_ID: muc_name,
        STREAM_TUBE_SERVICE: 'test'})

    e = q.expect('dbus-signal', signal='NewChannels')
    channels = e.args[0]
    assert len(channels) == 1

    # get the list of all channels to check that newly announced ones are in it
    all_channels = conn.Properties.Get(CONN_IFACE_REQUESTS, 'Channels',
        byte_arrays=True)

    path, props = channels[0]
    assert props[CHANNEL_TYPE] == CHANNEL_TYPE_STREAM_TUBE
    assert props[REQUESTED] == True
    assert props[INTERFACES] == [CHANNEL_IFACE_GROUP,
                                 CHANNEL_IFACE_TUBE]
    assert props[STREAM_TUBE_SERVICE] == 'test'
    assert props[INITIATOR_HANDLE] == conn1_self_handle
    assert props[INITIATOR_ID] == contact1_name
    assert props[TARGET_ID] == muc_name

    assert (path, props) in all_channels, (path, props)

    contact1_tube = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamTube')
    tube1_path = path

    state = contact1_tube.Properties.Get(CHANNEL_IFACE_TUBE, 'State')
    assert state == TUBE_CHANNEL_STATE_NOT_OFFERED

    call_async(q, contact1_tube.StreamTube, 'Offer',
            SOCKET_ADDRESS_TYPE_UNIX, dbus.ByteArray(server_socket_address),
            SOCKET_ACCESS_CONTROL_LOCALHOST, sample_parameters)

    q.expect_many(
        EventPattern('dbus-signal', signal='TubeChannelStateChanged',
            args=[TUBE_CHANNEL_STATE_OPEN]),
        EventPattern('dbus-return', method='Offer'))

    state = contact1_tube.Properties.Get(CHANNEL_IFACE_TUBE, 'State')
    assert state == TUBE_CHANNEL_STATE_OPEN

    # now let's get the text channel so we can invite contact2 using
    # the utility t.invite_to_muc
    _, path, _ = conn.Requests.EnsureChannel({
            CHANNEL_TYPE: CHANNEL_TYPE_TEXT,
            TARGET_HANDLE_TYPE: HT_ROOM,
            TARGET_ID: muc_name})
    text1 = wrap_channel(bus.get_object(conn.bus_name, path), 'Text')

    t.invite_to_muc(q, text1.Group, conn2, contact2_handle_on_conn1, contact1_handle_on_conn2)

    # tubes channel is created
    e = q.expect('dbus-signal', signal='NewChannels')
    channels = e.args[0]
    assert len(channels) == 1

    # get the list of all channels to check that newly announced ones are in it
    all_channels = conn2.Properties.Get(CONN_IFACE_REQUESTS, 'Channels',
        byte_arrays=True)

    path, props = channels[0]
    assert props[REQUESTED] == False
    assert props[INTERFACES] == [CHANNEL_IFACE_GROUP,
                                 CHANNEL_IFACE_TUBE]
    assert props[STREAM_TUBE_SERVICE] == 'test'
    assert props[TUBE_PARAMETERS] == sample_parameters

    assert (path, props) in all_channels, (path, props)

    contact2_tube = wrap_channel(bus.get_object(conn.bus_name, path),
                                         'StreamTube')
    tube2_path = path

    state = contact2_tube.Properties.Get(CHANNEL_IFACE_TUBE, 'State')
    assert state == TUBE_CHANNEL_STATE_LOCAL_PENDING

    # second connection: accept the tube (new API)
    unix_socket_adr = contact2_tube.StreamTube.Accept(
        SOCKET_ADDRESS_TYPE_UNIX, SOCKET_ACCESS_CONTROL_LOCALHOST, '',
        byte_arrays=True)

    state = contact2_tube.Properties.Get(CHANNEL_IFACE_TUBE, 'State')
    assert state == TUBE_CHANNEL_STATE_OPEN

    e = q.expect('dbus-signal', signal='TubeChannelStateChanged',
        path=tube2_path, args=[TUBE_CHANNEL_STATE_OPEN])

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

    call_async(q, contact1_tube, 'Close')
    _, e1, e2, _, _ = q.expect_many(
        EventPattern('dbus-return', method='Close'),
        EventPattern('dbus-signal', signal='ConnectionClosed', path=tube1_path),
        EventPattern('dbus-signal', signal='ConnectionClosed', path=tube2_path),
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

    conn_id, error, dbus_msg = e1.args
    assert conn_id == contact1_tube_conn_id
    assert error == CANCELLED

    conn_id, error, dbus_msg = e2.args
    assert conn_id == contact2_tube_conn_id
    assert error == CONNECTION_LOST

    call_async(q, contact2_tube, 'Close')
    q.expect_many(
        EventPattern('dbus-return', method='Close'),
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

    conn.Disconnect()
    conn2.Disconnect()

    # cleanup
    try:
        os.remove(server_socket_address)
    except OSError:
        pass # idgaf

if __name__ == '__main__':
    # increase timer because Clique takes some time to join an existing muc
    exec_test(test, timeout=60)
