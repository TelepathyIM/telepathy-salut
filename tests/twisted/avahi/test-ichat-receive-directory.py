import httplib
import urlparse
import dbus
import socket
import md5
import avahi
import BaseHTTPServer
import urllib

from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name

from xmppstream import setup_stream_listener, connect_to_stream, OutgoingXmppiChatStream
from servicetest import make_channel_proxy, EventPattern

from twisted.words.xish import domish

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

FILE_HASH_TYPE_NONE = 0
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

print "FIXME: This test fails if there is another LL XMPP instance running on the machine."
# exiting 77 causes automake to consider the test to have been skipped
raise SystemExit(77)

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

    requests_iface = dbus.Interface(conn, CONNECTION_INTERFACE_REQUESTS)

    # Create a connection to send the FT stanza
    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    e = q.expect('service-resolved', service = service)

    outbound = connect_to_stream(q, contact_name,
        self_handle_name, str(e.pt), e.port, OutgoingXmppiChatStream)

    e = q.expect('connection-result')
    assert e.succeeded, e.reason
    e = q.expect('stream-opened', connection = outbound)

    # Setup the HTTP server
    httpd = BaseHTTPServer.HTTPServer(('', 0), HTTPHandler)

    # connected to Salut, now send the IQ
    iq = domish.Element((None, 'iq'))
    iq['to'] = self_handle_name
    # no 'from' attribute
    iq['type'] = 'set'
    iq['id'] = 'iChat_A1FB5D95'
    query = iq.addElement(('jabber:iq:oob', 'query'))
    url = 'http://127.0.0.1:%u/gibber-file-transfer-0/my_directory/' % (httpd.server_port)
    url_node = query.addElement('url', content=url)
    url_node['type'] = 'directory'
    url_node['size'] = '1000'
    url_node['nfiles'] = '5'
    url_node['posixflags'] = '00000180'
    outbound.send(iq)

    # Send an error as we don't support directory transfer for now
    q.expect('stream-iq', iq_type='error')

class HTTPHandler(BaseHTTPServer.BaseHTTPRequestHandler):
    def do_GET(self):
        # is that the right file ?
        filename = self.path.rsplit('/', 2)[-1]
        assert filename == urllib.quote(FILE_NAME)

        self.send_response(200)
        self.send_header('Content-type', FILE_CONTENT_TYPE)
        self.end_headers()
        self.wfile.write(FILE_DATA)

if __name__ == '__main__':
    exec_test(test)
