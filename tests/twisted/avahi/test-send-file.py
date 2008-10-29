import httplib
import urlparse
import dbus
import socket
import md5

from saluttest import exec_test
from avahitest import AvahiAnnouncer
from avahitest import get_host_name

from xmppstream import setup_stream_listener
from servicetest import make_channel_proxy, EventPattern

from twisted.words.xish import domish

tp_name_prefix = 'org.freedesktop.Telepathy'
ft_name_prefix = '%s.Channel.Type.FileTransfer.DRAFT' % tp_name_prefix

CHANNEL_TYPE_FILE_TRANSFER = 'org.freedesktop.Telepathy.Channel.Type.FileTransfer.DRAFT'
HT_CONTACT = 1
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)

FT_STATE_NONE = 1
FT_STATE_NOT_OFFERED = 1
FT_STATE_ACCEPTED = 2
FT_STATE_LOCAL_PENDING = 3
FT_STATE_REMOTE_PENDING = 4
FT_STATE_OPEN = 5
FT_STATE_COMPLETED = 6
FT_STATE_CANCELLED = 7

FT_STATE_CHANGE_REASON_NONE = 0

FILE_HASH_TYPE_MD5 = 1

SOCKET_ADDRESS_TYPE_UNIX = 0
SOCKET_ADDRESS_TYPE_IPV4 = 2

SOCKET_ACCESS_CONTROL_LOCALHOST = 0

# File to Offer
FILE_DATA = "That works!"
FILE_SIZE = len(FILE_DATA)
FILE_NAME = 'test.txt'
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
    listener, port = setup_stream_listener(q, contact_name)

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

    requests_iface = dbus.Interface(conn, tp_name_prefix + '.Connection.Interface.Requests')

    # check if we can request FileTransfer channels
    properties = conn.GetAll(
        'org.freedesktop.Telepathy.Connection.Interface.Requests',
        dbus_interface='org.freedesktop.DBus.Properties')

    assert ({tp_name_prefix + '.Channel.ChannelType': CHANNEL_TYPE_FILE_TRANSFER,
             tp_name_prefix + '.Channel.TargetHandleType': HT_CONTACT},
            [tp_name_prefix + '.Channel.TargetHandle',
             tp_name_prefix + '.Channel.TargetID',
             ft_name_prefix + '.ContentType',
             ft_name_prefix + '.Filename',
             ft_name_prefix + '.Size',
             ft_name_prefix + '.ContentHashType',
             ft_name_prefix + '.ContentHash',
             ft_name_prefix + '.Description',
             ft_name_prefix + '.Date',
             ft_name_prefix + '.InitialOffset'],
         ) in properties.get('RequestableChannelClasses'),\
                 properties['RequestableChannelClasses']

    path, props = requests_iface.CreateChannel({
        tp_name_prefix + '.Channel.ChannelType': CHANNEL_TYPE_FILE_TRANSFER,
        tp_name_prefix + '.Channel.TargetHandleType': HT_CONTACT,
        tp_name_prefix + '.Channel.TargetHandle': handle,
        ft_name_prefix + '.ContentType': 'application/octet-stream',
        ft_name_prefix + '.Filename': FILE_NAME,
        ft_name_prefix + '.Size': FILE_SIZE,
        ft_name_prefix + '.ContentHashType': FILE_HASH_TYPE,
        ft_name_prefix + '.ContentHash': FILE_HASH,
        ft_name_prefix + '.Description': 'A nice file to test',
        ft_name_prefix + '.Date':  1225278834,
        ft_name_prefix + '.InitialOffset': 0,
        })

    # org.freedesktop.Telepathy.Channel D-Bus properties
    assert props[tp_name_prefix + '.Channel.ChannelType'] == CHANNEL_TYPE_FILE_TRANSFER
    assert props[tp_name_prefix + '.Channel.Interfaces'] == []
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == handle
    assert props[tp_name_prefix + '.Channel.TargetID'] == contact_name
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_CONTACT
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] == self_handle
    assert props[tp_name_prefix + '.Channel.InitiatorID'] == self_handle_name

    # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
    assert props[ft_name_prefix + '.State'] == FT_STATE_NOT_OFFERED
    assert props[ft_name_prefix + '.ContentType'] == 'application/octet-stream'
    assert props[ft_name_prefix + '.Filename'] == FILE_NAME
    assert props[ft_name_prefix + '.Size'] == FILE_SIZE
    assert props[ft_name_prefix + '.ContentHashType'] == FILE_HASH_TYPE
    assert props[ft_name_prefix + '.ContentHash'] == FILE_HASH
    assert props[ft_name_prefix + '.Description'] == 'A nice file to test'
    assert props[ft_name_prefix + '.Date'] == 1225278834
    # FIXME
    assert props[ft_name_prefix + '.AvailableSocketTypes'] == {}
    assert props[ft_name_prefix + '.TransferredBytes'] == 0
    assert props[ft_name_prefix + '.InitialOffset'] == 0

    channel = make_channel_proxy(conn, path, 'Channel')
    ft_channel = make_channel_proxy(conn, path, 'Channel.Type.FileTransfer.DRAFT')
    address = ft_channel.OfferFile(SOCKET_ADDRESS_TYPE_UNIX, SOCKET_ACCESS_CONTROL_LOCALHOST, "")

    conn_event, state_event, iq_event = q.expect_many(
        EventPattern('incoming-connection', listener = listener),
        EventPattern('dbus-signal', signal='FileTransferStateChanged'),
        EventPattern('stream-iq'))

    incoming = conn_event.connection

    state, reason = state_event.args
    assert state == FT_STATE_REMOTE_PENDING
    # FIXME: shouldn't it REQUESTED ?
    assert reason == FT_STATE_CHANGE_REASON_NONE

    assert iq_event.iq_type == 'set'
    assert iq_event.connection == incoming
    iq = iq_event.stanza
    assert iq['to'] == contact_name
    query = iq.firstChildElement()
    assert query.uri == 'jabber:iq:oob'
    url_node = query.firstChildElement()
    assert url_node['type'] == 'file'
    assert url_node['size'] == str(FILE_SIZE)
    url = url_node.children[0]
    assert url.endswith(FILE_NAME)
    # FIXME: check <desc> node

    reply = domish.Element(('', 'iq'))
    reply['to'] = iq['from']
    reply['from'] = iq['to']
    reply['type'] = 'result'
    reply['id'] = iq['id']
    incoming.send(reply)

    # Connect HTTP client to the CM and request the file
    _, host, file, _, _, _ = urlparse.urlparse(url)
    http = httplib.HTTPConnection(host)
    http.request('GET', file)

    e = q.expect('dbus-signal', signal='FileTransferStateChanged')
    state, reason = e.args
    assert state == FT_STATE_OPEN
    assert reason == FT_STATE_CHANGE_REASON_NONE

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(address)
    s.send(FILE_DATA)

    # FIXME: Why this is fired before TransferredBytesChanged?
    e = q.expect('dbus-signal', signal='FileTransferStateChanged')
    state, reason = e.args
    assert state == FT_STATE_COMPLETED
    assert reason == FT_STATE_CHANGE_REASON_NONE

    response = http.getresponse()
    assert (response.status, response.reason) == (200, 'OK')
    data = response.read(FILE_SIZE)
    # Did we received the right file?
    assert data == FILE_DATA

    transfered = False
    while not transfered:
        e = q.expect('dbus-signal', signal='TransferredBytesChanged')
        count = e.args[0]
        if count == FILE_SIZE:
            transfered = True

    channel.Close()
    q.expect('dbus-signal', signal='Closed')

if __name__ == '__main__':
    exec_test(test)
