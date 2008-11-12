import httplib
import urlparse
import dbus
import socket
import md5

from saluttest import exec_test
from avahitest import AvahiAnnouncer
from avahitest import get_host_name

from xmppstream import setup_stream_listener, IncomingXmppiChatStream
from servicetest import make_channel_proxy, EventPattern

from twisted.words.xish import domish, xpath

from dbus import PROPERTIES_IFACE

CONNECTION_INTERFACE_REQUESTS = 'org.freedesktop.Telepathy.Connection.Interface.Requests'
CHANNEL_INTERFACE ='org.freedesktop.Telepathy.Channel'
CHANNEL_TYPE_FILE_TRANSFER = 'org.freedesktop.Telepathy.Channel.Type.FileTransfer.DRAFT'

HT_CONTACT = 1
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)

FT_STATE_NONE = 0
FT_STATE_PENDING = 1
FT_STATE_ACCEPTED = 2
FT_STATE_OPEN = 3
FT_STATE_COMPLETED = 4
FT_STATE_CANCELLED = 5

FT_STATE_CHANGE_REASON_NONE = 0
FT_STATE_CHANGE_REASON_REQUESTED = 1
FT_STATE_CHANGE_REASON_LOCAL_STOPPED = 2
FT_STATE_CHANGE_REASON_REMOTE_STOPPED = 3
FT_STATE_CHANGE_REASON_LOCAL_ERROR = 4
FT_STATE_CHANGE_REASON_REMOTE_ERROR = 5

FILE_HASH_TYPE_MD5 = 1

SOCKET_ADDRESS_TYPE_UNIX = 0
SOCKET_ADDRESS_TYPE_IPV4 = 2

SOCKET_ACCESS_CONTROL_LOCALHOST = 0

# File to Offer
FILE_DATA = "That works!"
FILE_SIZE = len(FILE_DATA)
FILE_NAME = 'test.txt'
FILE_CONTENT_TYPE = 'text/plain'
FILE_DESCRIPTION = 'A nice file to test'
FILE_HASH_TYPE = FILE_HASH_TYPE_MD5
m = md5.new()
m.update(FILE_DATA)
FILE_HASH = m.hexdigest()

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(HT_CONTACT, [self_handle])[0]

    contact_name = "test-file-receiver@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name, protocol=IncomingXmppiChatStream)

    AvahiAnnouncer(contact_name, "_presence._tcp", port, basic_txt)

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

    requests_iface = dbus.Interface(conn, CONNECTION_INTERFACE_REQUESTS)

    # check if we can request FileTransfer channels
    properties = conn.GetAll(
        'org.freedesktop.Telepathy.Connection.Interface.Requests',
        dbus_interface='org.freedesktop.DBus.Properties')

    assert ({CHANNEL_INTERFACE + '.ChannelType': CHANNEL_TYPE_FILE_TRANSFER,
             CHANNEL_INTERFACE + '.TargetHandleType': HT_CONTACT},
            [CHANNEL_INTERFACE + '.TargetHandle',
             CHANNEL_INTERFACE + '.TargetID',
             CHANNEL_TYPE_FILE_TRANSFER + '.ContentType',
             CHANNEL_TYPE_FILE_TRANSFER + '.Filename',
             CHANNEL_TYPE_FILE_TRANSFER + '.Size',
             CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType',
             CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash',
             CHANNEL_TYPE_FILE_TRANSFER + '.Description',
             CHANNEL_TYPE_FILE_TRANSFER + '.Date',
             CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset'],
         ) in properties.get('RequestableChannelClasses'),\
                 properties['RequestableChannelClasses']

    path, props = requests_iface.CreateChannel({
        CHANNEL_INTERFACE + '.ChannelType': CHANNEL_TYPE_FILE_TRANSFER,
        CHANNEL_INTERFACE + '.TargetHandleType': HT_CONTACT,
        CHANNEL_INTERFACE + '.TargetHandle': handle,
        CHANNEL_TYPE_FILE_TRANSFER + '.ContentType': FILE_CONTENT_TYPE,
        CHANNEL_TYPE_FILE_TRANSFER + '.Filename': FILE_NAME,
        CHANNEL_TYPE_FILE_TRANSFER + '.Size': FILE_SIZE,
        CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType': FILE_HASH_TYPE,
        CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash': FILE_HASH,
        CHANNEL_TYPE_FILE_TRANSFER + '.Description': FILE_DESCRIPTION,
        CHANNEL_TYPE_FILE_TRANSFER + '.Date':  1225278834,
        CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset': 0,
        })

    # org.freedesktop.Telepathy.Channel D-Bus properties
    assert props[CHANNEL_INTERFACE + '.ChannelType'] == CHANNEL_TYPE_FILE_TRANSFER
    assert props[CHANNEL_INTERFACE + '.Interfaces'] == []
    assert props[CHANNEL_INTERFACE + '.TargetHandle'] == handle
    assert props[CHANNEL_INTERFACE + '.TargetID'] == contact_name
    assert props[CHANNEL_INTERFACE + '.TargetHandleType'] == HT_CONTACT
    assert props[CHANNEL_INTERFACE + '.Requested'] == True
    assert props[CHANNEL_INTERFACE + '.InitiatorHandle'] == self_handle
    assert props[CHANNEL_INTERFACE + '.InitiatorID'] == self_handle_name

    # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.State'] == FT_STATE_PENDING
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentType'] == FILE_CONTENT_TYPE
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Filename'] == FILE_NAME
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Size'] == FILE_SIZE
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType'] == FILE_HASH_TYPE
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash'] == FILE_HASH
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Description'] == FILE_DESCRIPTION
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Date'] == 1225278834
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.AvailableSocketTypes'] == \
        {SOCKET_ADDRESS_TYPE_UNIX: [SOCKET_ACCESS_CONTROL_LOCALHOST]}
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.TransferredBytes'] == 0
    assert props[CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset'] == 0

    channel = make_channel_proxy(conn, path, 'Channel')
    ft_channel = make_channel_proxy(conn, path, 'Channel.Type.FileTransfer.DRAFT')
    ft_props = dbus.Interface(bus.get_object(conn.object.bus_name, path), PROPERTIES_IFACE)

    address = ft_channel.ProvideFile(SOCKET_ADDRESS_TYPE_UNIX, SOCKET_ACCESS_CONTROL_LOCALHOST, "")

    conn_event, iq_event = q.expect_many(
        EventPattern('incoming-connection', listener = listener),
        EventPattern('stream-iq'))

    incoming = conn_event.connection

    assert iq_event.iq_type == 'set'
    assert iq_event.connection == incoming
    iq = iq_event.stanza
    assert iq['to'] == contact_name
    query = iq.firstChildElement()
    assert query.uri == 'jabber:iq:oob'
    url_node = xpath.queryForNodes("/iq/query/url",  iq)[0]
    assert url_node['type'] == 'file'
    assert url_node['size'] == str(FILE_SIZE)
    assert url_node['mimeType'] == FILE_CONTENT_TYPE
    url = url_node.children[0]
    assert url.endswith(FILE_NAME)
    desc_node = xpath.queryForNodes("/iq/query/desc",  iq)[0]
    desc = desc_node.children[0]
    assert desc == FILE_DESCRIPTION

    # Receiver declines the file offer
    reply = domish.Element(('', 'iq'))
    reply['to'] = iq['from']
    reply['type'] = 'error'
    reply['id'] = iq['id']
    error_node = reply.addElement((None, 'error'), content='User declined to receive the file')
    error_node['type'] = '406'
    incoming.send(reply)

    e = q.expect('dbus-signal', signal='FileTransferStateChanged')
    state, reason = e.args
    assert state == FT_STATE_CANCELLED, state
    assert reason == FT_STATE_CHANGE_REASON_REMOTE_STOPPED

    transferred = ft_props.Get(CHANNEL_TYPE_FILE_TRANSFER, 'TransferredBytes')
    # no byte has been transferred as the file was declined
    assert transferred == 0

    channel.Close()
    q.expect('dbus-signal', signal='Closed')

if __name__ == '__main__':
    exec_test(test)
