import httplib
import urlparse
import dbus
import socket
import md5
import avahi
import BaseHTTPServer

from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, EventPattern

from twisted.words.xish import domish

tp_name_prefix = 'org.freedesktop.Telepathy'
ft_name_prefix = '%s.Channel.Type.FileTransfer.DRAFT' % tp_name_prefix

CHANNEL_TYPE_FILE_TRANSFER = 'org.freedesktop.Telepathy.Channel.Type.FileTransfer.DRAFT'
HT_CONTACT = 1
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)

FT_STATE_NONE = 0
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
FILE_DATA = "What a nice file"
FILE_SIZE = len(FILE_DATA)
FILE_NAME = 'foo.txt'
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

    contact_name = "test-file-sender@" + get_host_name()
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

    # Create a connection to send the FT stanza
    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    e = q.expect('service-resolved', service = service)

    outbound = connect_to_stream(q, contact_name,
        self_handle_name, str(e.pt), e.port)

    e = q.expect('connection-result')
    assert e.succeeded, e.reason
    e = q.expect('stream-opened', connection = outbound)

    # Setup the HTTP server
    httpd = BaseHTTPServer.HTTPServer(('', 0), HTTPHandler)

    # connected to Salut, now send the IQ
    iq = domish.Element((None, 'iq'))
    iq['to'] = self_handle_name
    iq['from'] = contact_name
    iq['type'] = 'set'
    iq['id'] = 'gibber-file-transfer-0'
    query = iq.addElement(('jabber:iq:oob', 'query'))
    url = 'http://127.0.0.1:%u/gibber-file-transfer-0/%s' % (httpd.server_port, FILE_NAME)
    url_node = query.addElement('url', content=url)
    url_node['type'] = 'file'
    url_node['size'] = str(FILE_SIZE)
    query.addElement('desc', content=FILE_DESCRIPTION)
    outbound.send(iq)

    e =q.expect('dbus-signal', signal='NewChannels')
    channels = e.args[0]
    assert len(channels) == 1
    path, props = channels[0]

    # check channel properties
    # org.freedesktop.Telepathy.Channel D-Bus properties
    assert props[tp_name_prefix + '.Channel.ChannelType'] == CHANNEL_TYPE_FILE_TRANSFER
    assert props[tp_name_prefix + '.Channel.Interfaces'] == []
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == handle
    assert props[tp_name_prefix + '.Channel.TargetID'] == contact_name
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_CONTACT
    assert props[tp_name_prefix + '.Channel.Requested'] == False
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] == handle
    assert props[tp_name_prefix + '.Channel.InitiatorID'] == contact_name

    # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
    assert props[ft_name_prefix + '.State'] == FT_STATE_LOCAL_PENDING
    # The protocol doesn't allow us to send the content-type so use
    # octet-stream as said in the spec.
    assert props[ft_name_prefix + '.ContentType'] == 'application/octet-stream'
    assert props[ft_name_prefix + '.Filename'] == FILE_NAME
    assert props[ft_name_prefix + '.Size'] == FILE_SIZE
    # FIXME: How should we deal with the hash properties?
    #assert props[ft_name_prefix + '.ContentHashType'] == FILE_HASH_TYPE
    #assert props[ft_name_prefix + '.ContentHash'] == FILE_HASH
    # FIXME: Salut should parse the <desc> node and set the description
    #assert props[ft_name_prefix + '.Description'] == FILE_DESCRIPTION
    # FIXME: How should we deal with the Date property?
    #assert props[ft_name_prefix + '.Date'] == 1225278834
    assert props[ft_name_prefix + '.AvailableSocketTypes'] == \
        {SOCKET_ADDRESS_TYPE_UNIX: [SOCKET_ACCESS_CONTROL_LOCALHOST]}
    assert props[ft_name_prefix + '.TransferredBytes'] == 0
    assert props[ft_name_prefix + '.InitialOffset'] == 0

    channel = make_channel_proxy(conn, path, 'Channel')
    ft_channel = make_channel_proxy(conn, path, 'Channel.Type.FileTransfer.DRAFT')

    address = ft_channel.AcceptFile(SOCKET_ADDRESS_TYPE_UNIX, SOCKET_ACCESS_CONTROL_LOCALHOST, "", 0)

    e = q.expect('dbus-signal', signal='FileTransferStateChanged')
    state, reason = e.args
    assert state == FT_STATE_ACCEPTED
    # FIXME: shouldn't it be requested?
    assert reason == FT_STATE_CHANGE_REASON_NONE

    e = q.expect('dbus-signal', signal='FileTransferStateChanged')
    state, reason = e.args
    assert state == FT_STATE_OPEN
    assert reason == FT_STATE_CHANGE_REASON_NONE

    # Connect to Salut's socket
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(address)

    httpd.handle_request()

    # Read the file from Salut's socket
    data = ''
    read = 0
    while read < FILE_SIZE:
        data += s.recv(FILE_SIZE - read)
        read = len(data)
    assert data == FILE_DATA

    e = q.expect('dbus-signal', signal='TransferredBytesChanged')
    count = e.args[0]
    while count < FILE_SIZE:
        # Catch TransferredBytesChanged until we transfered all the data
        e = q.expect('dbus-signal', signal='TransferredBytesChanged')
        count = e.args[0]

    e = q.expect('dbus-signal', signal='FileTransferStateChanged')
    state, reason = e.args
    assert state == FT_STATE_COMPLETED
    assert reason == FT_STATE_CHANGE_REASON_NONE

    channel.Close()
    q.expect('dbus-signal', signal='Closed')

class HTTPHandler(BaseHTTPServer.BaseHTTPRequestHandler):
    def do_GET(self):
        # is that the right file ?
        filename = self.path.rsplit('/', 2)[-1]
        assert filename == FILE_NAME

        self.send_response(200)
        self.send_header('Content-type', FILE_CONTENT_TYPE)
        self.end_headers()
        self.wfile.write(FILE_DATA)

if __name__ == '__main__':
    exec_test(test)
