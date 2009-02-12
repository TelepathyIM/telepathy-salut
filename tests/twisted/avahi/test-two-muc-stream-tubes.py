from saluttest import exec_test, make_connection, wait_for_contact_list
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi
import dbus
import os
import errno
import string

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, Event, call_async, EventPattern

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

muc_name = "test-two-muc-stream-tubes"
muc2_name = "test-two-muc-stream-tubes-2"

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

    # FIXME: this is a hack to be sure to have all the contact list channels
    # announced so they won't interfere with the muc ones announces.
    wait_for_contact_list(q, conn)

    # second connection: connect
    conn2_params = {
        'published-name': 'testsuite2',
        'first-name': 'test2',
        'last-name': 'suite2',
        }
    contact2_name = "testsuite2" + "@" + get_host_name()
    conn2 = make_connection(bus, lambda x: None, conn2_params)
    conn2.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # FIXME: this is a hack to be sure to have all the contact list channels
    # announced so they won't interfere with the muc ones announces.
    # publish
    q.expect('dbus-signal', signal='NewChannel', path=conn2.object_path)
    # subscribe
    q.expect('dbus-signal', signal='NewChannel', path=conn2.object_path)
    # known
    q.expect('dbus-signal', signal='NewChannel', path=conn2.object_path)

    # first connection: get the contact list
    publish_handle = conn.RequestHandles(HT_CONTACT_LIST, ["publish"])[0]
    conn1_publish = conn.RequestChannel(
        "org.freedesktop.Telepathy.Channel.Type.ContactList",
        HT_CONTACT_LIST, publish_handle, False)
    conn1_publish_proxy = bus.get_object(conn.bus_name, conn1_publish)

    # second connection: get the contact list
    publish_handle = conn2.RequestHandles(HT_CONTACT_LIST, ["publish"])[0]
    conn2_publish = conn2.RequestChannel(
        "org.freedesktop.Telepathy.Channel.Type.ContactList",
        HT_CONTACT_LIST, publish_handle, False)
    conn2_publish_proxy = bus.get_object(conn2.bus_name, conn2_publish)

    # first connection: wait to see contact2
    # The signal MembersChanged may be already emitted... check the Members
    # property first
    contact2_handle_on_conn1 = 0
    conn1_members = conn1_publish_proxy.Get(
            'org.freedesktop.Telepathy.Channel.Interface.Group', 'Members',
            dbus_interface='org.freedesktop.DBus.Properties')
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
    conn2_members = conn2_publish_proxy.Get(
            'org.freedesktop.Telepathy.Channel.Interface.Group', 'Members',
            dbus_interface='org.freedesktop.DBus.Properties')
    for h in conn2_members:
        name = conn2.InspectHandles(HT_CONTACT, [h])[0]
        if name == contact1_name:
            contact1_handle_on_conn2 = h
    while contact1_handle_on_conn2 == 0:
        e = q.expect('dbus-signal', signal='MembersChanged', path=conn2_publish)
        for h in e.args[1]:
            name = conn2.InspectHandles(HT_CONTACT, [h])[0]
            if name == contact1_name:
                contact1_handle_on_conn2 = h

    # first connection: join muc
    conn1_self_handle = conn.GetSelfHandle()
    muc_handle1 = conn.RequestHandles(HT_ROOM, [muc_name])[0]
    path = conn.RequestChannel(CHANNEL_TYPE_TEXT, HT_ROOM, muc_handle1, True)
    # added as remote pending
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [], [], [], [conn1_self_handle], conn1_self_handle, 0])
    # added as member
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [conn1_self_handle], [], [], [], conn1_self_handle, 0])
    group1 = make_channel_proxy(conn, path, "Channel.Interface.Group")

    # first connection: invite contact2
    group1.AddMembers([contact2_handle_on_conn1], "Let's tube!")

    # channel is created on conn2
    e = q.expect('dbus-signal', signal='NewChannel', path=conn2.object_path)
    path = e.args[0]
    group2 = make_channel_proxy(conn, path, "Channel.Interface.Group")

    # we are invited to the muc
    # added as local pending
    conn2_self_handle = conn2.GetSelfHandle()
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=["Let's tube!", [], [], [conn2_self_handle], [], contact1_handle_on_conn2, 4])

    # second connection: accept the invite
    group2.AddMembers([conn2_self_handle], "")

    # added as remote pending
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [], [], [], [conn2_self_handle], conn2_self_handle, 0])

    # added as member
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [conn2_self_handle], [], [], [], conn2_self_handle, 0])

    # first connection: offer a muc stream tube (old API)
    tubes1_path = conn.RequestChannel(CHANNEL_TYPE_TUBES, HT_ROOM, muc_handle1, True)
    contact1_tubes_channel = make_channel_proxy(conn, tubes1_path, "Channel.Type.Tubes")

    q.expect('dbus-signal', signal='NewChannel',
        args=[tubes1_path, CHANNEL_TYPE_TUBES, HT_ROOM, muc_handle1, True])

    conn1_tube_id = contact1_tubes_channel.OfferStreamTube("http", sample_parameters,
            SOCKET_ADDRESS_TYPE_UNIX, dbus.ByteArray(server_socket_address),
            SOCKET_ACCESS_CONTROL_LOCALHOST, "")

    e = q.expect('dbus-signal', signal='NewTube', path=tubes1_path)
    tube = e.args
    assert tube[1] == conn1_self_handle    # initiator
    assert tube[2] == 1                    # type = stream tube
    assert tube[3] == 'http'               # service
    assert tube[4] == sample_parameters    # paramaters
    assert tube[5] == TUBE_CHANNEL_STATE_OPEN

    contact2_channeltype = None
    while contact2_channeltype == None:
        e = q.expect('dbus-signal', signal='NewChannel')
        if (e.args[1] == CHANNEL_TYPE_TUBES) and (e.path.endswith("testsuite2") == True):
            tubes2_path = e.args[0]
            contact2_channeltype = e.args[1]

    contact2_tubes_channel = make_channel_proxy(conn2, tubes2_path, "Channel.Type.Tubes")

    contact2_tubes = contact2_tubes_channel.ListTubes()
    assert len(contact2_tubes) == 1
    contact2_tube = contact2_tubes[0]
    assert contact2_tube[0] is not None # tube id
    conn2_tube_id = contact2_tube[0]
    assert contact2_tube[1] is not None # initiator
    assert contact2_tube[2] == 1 # type = stream tube
    assert contact2_tube[3] == 'http' # service = http
    assert contact2_tube[4] is not None # parameters
    assert contact2_tube[5] == 0, contact2_tube[5] # status = local pending

    # second connection: accept the tube (old API)
    unix_socket_adr = contact2_tubes_channel.AcceptStreamTube(
            contact2_tube[0], 0, 0, '', byte_arrays=True)

    e = q.expect('dbus-signal', signal='TubeStateChanged', path=tubes2_path)
    id, state = e.args
    assert id == conn2_tube_id
    assert state == TUBE_CHANNEL_STATE_OPEN

    client = ClientCreator(reactor, ClientGreeter)
    client.connectUNIX(unix_socket_adr).addCallback(client_connected_cb)

    # server got the connection
    _, e = q.expect_many(
        EventPattern('server-connected'),
        EventPattern('client-connected'))

    client_transport = e.transport

    e = q.expect('dbus-signal', signal='StreamTubeNewConnection', path=tubes1_path)
    id, handle = e.args
    assert id == conn1_tube_id
    assert handle == contact2_handle_on_conn1

    # client receives server's welcome message
    e = q.expect('client-data-received')
    assert e.data == SERVER_WELCOME_MSG

    client_transport.write(test_string)

    e = q.expect('server-data-received')
    assert e.data == test_string

    e = q.expect('client-data-received')
    assert e.data == string.swapcase(test_string)

    # contact1 closes the tube
    contact1_tubes_channel.CloseTube(conn1_tube_id)
    q.expect('dbus-signal', signal='TubeClosed', args=[conn1_tube_id])

    # contact2 closes the tube
    contact2_tubes_channel.CloseTube(conn2_tube_id)
    q.expect('dbus-signal', signal='TubeClosed', args=[conn2_tube_id])

    # Now contact1 will create a new muc stream tube to another room using the
    # new API

    # Can we request muc stream tubes?
    properties = conn.GetAll(CONN_IFACE_REQUESTS, dbus_interface=PROPERTIES_IFACE)

    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_ROOM},
         [TARGET_HANDLE, TARGET_ID, STREAM_TUBE_SERVICE]
        ) in properties.get('RequestableChannelClasses'),\
                 properties['RequestableChannelClasses']

    # request a stream tube channel (new API)
    requestotron = dbus.Interface(conn, CONN_IFACE_REQUESTS)

    requestotron.CreateChannel({
        CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
        TARGET_HANDLE_TYPE: HT_ROOM,
        TARGET_ID: muc2_name,
        STREAM_TUBE_SERVICE: 'test'})

    e = q.expect('dbus-signal', signal='NewChannels')
    channels = e.args[0]
    assert len(channels) == 3

    got_text, got_tubes, got_tube = False, False, False
    for path, props in channels:
        if props[CHANNEL_TYPE] == CHANNEL_TYPE_TEXT:
            got_text = True
            assert props[REQUESTED] == False
            group1 = make_channel_proxy(conn, path, "Channel.Interface.Group")
            txt_path = path
        elif props[CHANNEL_TYPE] == CHANNEL_TYPE_TUBES:
            got_tubes = True
            assert props[REQUESTED] == False
            assert props[INTERFACES] == [CHANNEL_IFACE_GROUP]
        elif props[CHANNEL_TYPE] == CHANNEL_TYPE_STREAM_TUBE:
            got_tube = True
            assert props[REQUESTED] == True
            assert props[INTERFACES] == [CHANNEL_IFACE_GROUP, CHANNEL_IFACE_TUBE]
            assert props[STREAM_TUBE_SERVICE] == 'test'

            contact1_tube = bus.get_object(conn.bus_name, path)
            contact1_stream_tube = make_channel_proxy(conn, path, "Channel.Type.StreamTube.DRAFT")
            contact1_tube_channel = make_channel_proxy(conn, path, "Channel")
            tube1_path = path
        else:
            assert False

        assert props[INITIATOR_HANDLE] == conn1_self_handle
        assert props[INITIATOR_ID] == contact1_name
        assert props[TARGET_ID] == muc2_name

    assert got_text
    assert got_tubes
    assert got_tube

    state = contact1_stream_tube.Get(CHANNEL_IFACE_TUBE, 'State', dbus_interface=PROPERTIES_IFACE)
    assert state == TUBE_CHANNEL_STATE_NOT_OFFERED

    # added as member
    q.expect('dbus-signal', signal='MembersChanged', path=txt_path,
        args=['', [conn1_self_handle], [], [], [], conn1_self_handle, 0])

    call_async(q, contact1_stream_tube, 'OfferStreamTube', SOCKET_ADDRESS_TYPE_UNIX,
            dbus.ByteArray(server_socket_address), SOCKET_ACCESS_CONTROL_LOCALHOST, "", sample_parameters)

    q.expect_many(
        EventPattern('dbus-signal', signal='TubeChannelStateChanged', args=[TUBE_CHANNEL_STATE_OPEN]),
        EventPattern('dbus-return', method='OfferStreamTube'))

    state = contact1_stream_tube.Get(CHANNEL_IFACE_TUBE, 'State', dbus_interface=PROPERTIES_IFACE)
    assert state == TUBE_CHANNEL_STATE_OPEN

    # invite contact2 to the room
    group1.AddMembers([contact2_handle_on_conn1], "Let's tube!")

    # channel is created on conn2
    e = q.expect('dbus-signal', signal='NewChannel', path=conn2.object_path)
    path = e.args[0]
    group2 = make_channel_proxy(conn, path, "Channel.Interface.Group")

    # we are invited to the muc
    # added as local pending
    conn2_self_handle = conn2.GetSelfHandle()
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=["Let's tube!", [], [], [conn2_self_handle], [], contact1_handle_on_conn2, 4])

    # second connection: accept the invite
    group2.AddMembers([conn2_self_handle], "")

    # added as remote pending
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [], [], [], [conn2_self_handle], conn2_self_handle, 0])

    # added as member
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [conn2_self_handle], [], [], [], conn2_self_handle, 0])

    # tubes channel is created
    e = q.expect('dbus-signal', signal='NewChannels')
    channels = e.args[0]
    assert len(channels) == 2

    got_tubes, got_tube = False, False
    for path, props in channels:
        if props[CHANNEL_TYPE] == CHANNEL_TYPE_TUBES:
            got_tubes = True
            assert props[REQUESTED] == False
            assert props[INTERFACES] == [CHANNEL_IFACE_GROUP]
        elif props[CHANNEL_TYPE] == CHANNEL_TYPE_STREAM_TUBE:
            got_tube = True
            assert props[REQUESTED] == False
            assert props[INTERFACES] == [CHANNEL_IFACE_GROUP, CHANNEL_IFACE_TUBE]
            assert props[STREAM_TUBE_SERVICE] == 'test'
            assert props[TUBE_PARAMETERS] == sample_parameters

            contact2_tube = bus.get_object(conn.bus_name, path)
            contact2_stream_tube = make_channel_proxy(conn, path, "Channel.Type.StreamTube.DRAFT")
            contact2_tube_channel = make_channel_proxy(conn, path, "Channel")
            tube2_path = path
        else:
            assert False

        assert props[INITIATOR_HANDLE] == contact1_handle_on_conn2
        assert props[INITIATOR_ID] == contact1_name
        assert props[TARGET_ID] == muc2_name

    assert got_tubes
    assert got_tube

    state = contact2_tube.Get(CHANNEL_IFACE_TUBE, 'State', dbus_interface=PROPERTIES_IFACE)
    assert state == TUBE_CHANNEL_STATE_LOCAL_PENDING

    # second connection: accept the tube (new API)
    unix_socket_adr = contact2_stream_tube.AcceptStreamTube(SOCKET_ADDRESS_TYPE_UNIX,
        SOCKET_ACCESS_CONTROL_LOCALHOST, '', byte_arrays=True)

    state = contact2_tube.Get(CHANNEL_IFACE_TUBE, 'State', dbus_interface=PROPERTIES_IFACE)
    assert state == TUBE_CHANNEL_STATE_OPEN

    e = q.expect('dbus-signal', signal='TubeChannelStateChanged', path=tube2_path,
        args=[TUBE_CHANNEL_STATE_OPEN])

    client = ClientCreator(reactor, ClientGreeter)
    client.connectUNIX(unix_socket_adr).addCallback(client_connected_cb)

    # server got the connection
    _, e = q.expect_many(
        EventPattern('server-connected'),
        EventPattern('client-connected'))

    client_transport = e.transport

    e = q.expect('dbus-signal', signal='StreamTubeNewConnection', path=tube1_path)
    handle = e.args[0]
    assert handle == contact2_handle_on_conn1

    # client receives server's welcome message
    e = q.expect('client-data-received')
    assert e.data == SERVER_WELCOME_MSG

    client_transport.write(test_string)

    e = q.expect('server-data-received')
    assert e.data == test_string

    e = q.expect('client-data-received')
    assert e.data == string.swapcase(test_string)

    call_async(q, contact1_tube_channel, 'Close')
    q.expect_many(
        EventPattern('dbus-return', method='Close'),
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='TubeClosed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

    call_async(q, contact2_tube_channel, 'Close')
    q.expect_many(
        EventPattern('dbus-return', method='Close'),
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='TubeClosed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

    conn.Disconnect()
    conn2.Disconnect()

if __name__ == '__main__':
    # increase timer because Clique takes some time to join an existing muc
    exec_test(test, timeout=60)
