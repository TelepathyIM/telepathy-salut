from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi
import dbus
import os
import errno
import string

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, Event, EventPattern, call_async, \
         tp_name_prefix, sync_dbus

from twisted.words.xish import xpath, domish
from twisted.internet.protocol import Factory, Protocol, ClientCreator
from twisted.internet import reactor

from constants import INVALID_ARGUMENT, NOT_IMPLEMENTED

PUBLISHED_NAME="test-tube"

CHANNEL_TYPE_TUBES = "org.freedesktop.Telepathy.Channel.Type.Tubes"
HT_CONTACT = 1
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)
SOCKET_ADDRESS_TYPE_UNIX = dbus.UInt32(0)
SOCKET_ADDRESS_TYPE_IPV4 = dbus.UInt32(2)
SOCKET_ADDRESS_TYPE_IPV6 = dbus.UInt32(3)
SOCKET_ACCESS_CONTROL_LOCALHOST = dbus.UInt32(0)

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

test_string = "This string travels on a tube !"

def check_conn_properties(q, bus, conn, channel_list=None):
    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')

    if channel_list == None:
        assert properties.get('Channels') == [], properties['Channels']
    else:
        for i in channel_list:
            assert i in properties['Channels'], \
                (i, properties['Channels'])

    assert ({'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Tubes',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': HT_CONTACT,
             },
             ['org.freedesktop.Telepathy.Channel.TargetHandle',
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']
    assert ({'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': HT_CONTACT,
             },
             ['org.freedesktop.Telepathy.Channel.TargetHandle',
              'org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT.Parameters',
              'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service',
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

def check_channel_properties(q, bus, conn, channel, channel_type,
        contact_handle, contact_id, state=None):
    # Exercise basic Channel Properties from spec 0.17.7
    # on the channel of type channel_type
    channel_props = channel.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == contact_handle,\
            (channel_props.get('TargetHandle'), contact_handle)
    assert channel_props.get('TargetHandleType') == HT_CONTACT,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            'org.freedesktop.Telepathy.Channel.Type.' + channel_type,\
            channel_props.get('ChannelType')
    assert 'Interfaces' in channel_props, channel_props
    assert 'org.freedesktop.Telepathy.Channel.Interface.Group' not in \
            channel_props['Interfaces'], \
            channel_props['Interfaces']
    assert channel_props['TargetID'] == contact_id

    if channel_type == "Tubes":
        assert state is None
    else:
        assert state is not None
        tube_props = channel.GetAll(
                'org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT',
                dbus_interface='org.freedesktop.DBus.Properties')
        assert tube_props['Status'] == state, tube_props['Status']
        # no strict check but at least check the properties exist
        assert tube_props.has_key('Parameters')

    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(HT_CONTACT, [self_handle])[0]

    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == self_handle_name
    assert channel_props['InitiatorHandle'] == self_handle


def check_NewChannel_signal(old_sig, channel_type, chan_path, contact_handle):
    assert old_sig[0] == chan_path, old_sig[0]
    assert old_sig[1] == tp_name_prefix + '.Channel.Type.' + channel_type
    assert old_sig[2] == HT_CONTACT
    assert old_sig[3] == contact_handle
    assert old_sig[4] == True      # suppress handler

def check_NewChannels_signal(conn, new_sig, channel_type, chan_path, contact_handle,
        contact_id, initiator_handle):
    assert len(new_sig) == 1
    assert len(new_sig[0]) == 1        # one channel
    assert len(new_sig[0][0]) == 2     # two struct members
    assert new_sig[0][0][0] == chan_path
    emitted_props = new_sig[0][0][1]

    initiator_name =  conn.InspectHandles(HT_CONTACT, [initiator_handle])[0]
    assert emitted_props[tp_name_prefix + '.Channel.ChannelType'] ==\
            tp_name_prefix + '.Channel.Type.' + channel_type
    assert emitted_props[tp_name_prefix + '.Channel.TargetHandleType'] == \
            HT_CONTACT
    assert emitted_props[tp_name_prefix + '.Channel.TargetHandle'] ==\
            contact_handle
    assert emitted_props[tp_name_prefix + '.Channel.TargetID'] == \
            contact_id
    assert emitted_props[tp_name_prefix + '.Channel.Requested'] == True
    assert emitted_props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == initiator_handle
    assert emitted_props[tp_name_prefix + '.Channel.InitiatorID'] == \
            initiator_name

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


    check_conn_properties(q, bus, conn)

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

    # NewChannels would be emitted for the contact list channels, we don't
    # want this to interfere with the NewChannels signals for the requested
    # tubes channel
    sync_dbus(bus, q, conn)

    # old requestotron
    call_async(q, conn, 'RequestChannel',
            CHANNEL_TYPE_TUBES, HT_CONTACT, handle, True);

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 1
    chan_path = ret.value[0]

    check_NewChannel_signal(old_sig.args, "Tubes", chan_path, handle)
    check_NewChannels_signal(conn, new_sig.args, "Tubes", chan_path,
            handle, contact_name, conn.GetSelfHandle())
    emitted_props = new_sig.args[0][0][1]
    old_tubes_channel_properties = new_sig.args[0][0]

    check_conn_properties(q, bus, conn, [old_tubes_channel_properties])

    # new requestotron
    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')

    # Try to CreateChannel with unknown properties
    # Salut must return an error
    try:
        requestotron.CreateChannel(
            {'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
             'org.freedesktop.Telepathy.Channel.TargetHandleType':
                HT_CONTACT,
             'org.freedesktop.Telepathy.Channel.TargetHandle':
                handle,
             'this.property.does.not.exist':
                'this.value.should.not.exist'
            })
    except dbus.DBusException, e:
        assert e.get_dbus_name() == NOT_IMPLEMENTED, e.get_dbus_name()
    else:
        assert False

    # CreateChannel failed, we expect no new channel
    check_conn_properties(q, bus, conn, [old_tubes_channel_properties])

    # Try to CreateChannel with missing properties ("Service")
    # Salut must return an error
    try:
        requestotron.CreateChannel(
            {'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
             'org.freedesktop.Telepathy.Channel.TargetHandleType':
                HT_CONTACT,
             'org.freedesktop.Telepathy.Channel.TargetHandle':
                handle
            });
    except dbus.DBusException, e:
        assert e.get_dbus_name() == INVALID_ARGUMENT, e.get_dbus_name()
    else:
        assert False
    # CreateChannel failed, we expect no new channel
    check_conn_properties(q, bus, conn, [old_tubes_channel_properties])

    # Try to CreateChannel with correct properties
    # Salut must succeed
    call_async(q, requestotron, 'CreateChannel',
            {'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
             'org.freedesktop.Telepathy.Channel.TargetHandleType':
                HT_CONTACT,
             'org.freedesktop.Telepathy.Channel.TargetHandle':
                handle,
             'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service':
                "newecho",
             'org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT.Parameters':
                dbus.Dictionary({'foo': 'bar'}, signature='sv'),
            });
    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 2 # CreateChannel returns 2 values: o, a{sv}
    new_chan_path = ret.value[0]
    new_chan_prop_asv = ret.value[1]
    # The path of the Channel.Type.Tubes object MUST be different to the path
    # of the Channel.Type.StreamTube object !
    assert chan_path != new_chan_path

    check_NewChannel_signal(old_sig.args, "StreamTube.DRAFT", \
            new_chan_path, handle)
    check_NewChannels_signal(conn, new_sig.args, "StreamTube.DRAFT", new_chan_path, \
            handle, contact_name, conn.GetSelfHandle())
    stream_tube_channel_properties = new_sig.args[0][0]

    check_conn_properties(q, bus, conn,
            [old_tubes_channel_properties, stream_tube_channel_properties])

    assert stream_tube_channel_properties[1]['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] == \
        'newecho'
    assert stream_tube_channel_properties[1]['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.SupportedSocketTypes'] == \
        {SOCKET_ADDRESS_TYPE_UNIX: [SOCKET_ACCESS_CONTROL_LOCALHOST],
         SOCKET_ADDRESS_TYPE_IPV4: [SOCKET_ACCESS_CONTROL_LOCALHOST],
         SOCKET_ADDRESS_TYPE_IPV6: [SOCKET_ACCESS_CONTROL_LOCALHOST]}

    # continue
    tubes_channel = make_channel_proxy(conn, chan_path, "Channel.Type.Tubes")
    tube_channel = make_channel_proxy(conn, new_chan_path,
            "Channel.Type.StreamTube.DRAFT")
    check_channel_properties(q, bus, conn, tubes_channel, "Tubes", handle,
            contact_name)
    check_channel_properties(q, bus, conn, tube_channel, "StreamTube.DRAFT",
            handle, contact_name, 3)

    tube_id = tubes_channel.OfferStreamTube("http", sample_parameters,
            SOCKET_ADDRESS_TYPE_UNIX, dbus.ByteArray(server_socket_address),
            SOCKET_ACCESS_CONTROL_LOCALHOST, "")

    e = q.expect('stream-iq')
    iq_tube = xpath.queryForNodes('/iq/tube', e.stanza)[0]
    transport = xpath.queryForNodes('/iq/tube/transport', e.stanza)[0]
    assert iq_tube.attributes['type'] == 'stream'
    assert iq_tube.attributes['service'] == 'http', \
        iq_tube.attributes['service']
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

    # find the right host/IP address because Salut checks it
    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(HT_CONTACT, [self_handle])[0]
    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()
    e = q.expect('service-resolved', service = service)
    host_name = e.host_name

    client = ClientCreator(reactor, ClientGreeter)
    client.connectTCP(host_name, port).addCallback(client_connected_cb)

    e = q.expect('client-connected')
    client_transport = e.transport
    client_transport.write(test_string)

    e = q.expect('server-data-received')
    assert e.data == test_string

    e = q.expect('client-data-received')
    assert e.data == string.swapcase(test_string)

    # Close the tubes propertly
    for i in tubes_channel.ListTubes():
        tubes_channel.CloseTube(i[0])
    conn.Disconnect()

if __name__ == '__main__':
    exec_test(test)
