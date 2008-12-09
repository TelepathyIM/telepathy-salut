import dbus
import socket
import md5
import avahi
import BaseHTTPServer
import urllib
import httplib
import urlparse

from avahitest import AvahiAnnouncer, AvahiListener, get_host_name

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, EventPattern

from twisted.words.xish import domish, xpath

from dbus import PROPERTIES_IFACE

CONNECTION_INTERFACE_REQUESTS = 'org.freedesktop.Telepathy.Connection.Interface.Requests'
CHANNEL_INTERFACE ='org.freedesktop.Telepathy.Channel'
CHANNEL_TYPE_FILE_TRANSFER = 'org.freedesktop.Telepathy.Channel.Type.FileTransfer.DRAFT'
HT_CONTACT = 1
HT_CONTACT_LIST = 3

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

FILE_HASH_TYPE_NONE = 0
FILE_HASH_TYPE_MD5 = 1
FILE_HASH_TYPE_SHA1 = 2
FILE_HASH_TYPE_SHA256 = 3

SOCKET_ADDRESS_TYPE_UNIX = 0
SOCKET_ADDRESS_TYPE_ABSTRACT_UNIX = 1
SOCKET_ADDRESS_TYPE_IPV4 = 2
SOCKET_ADDRESS_TYPE_IPV6 = 3

SOCKET_ACCESS_CONTROL_LOCALHOST = 0
SOCKET_ACCESS_CONTROL_PORT = 1
SOCKET_ACCESS_CONTROL_NETMASK = 2
SOCKET_ACCESS_CONTROL_CREDENTIALS = 3

class File(object):
    DEFAULT_DATA = "What a nice file"
    DEFAULT_NAME = "The foo.txt"
    DEFAULT_CONTENT_TYPE = 'text/plain'
    DEFAULT_DESCRIPTION = "A nice file to test"

    def __init__(self, data=DEFAULT_DATA, name=DEFAULT_NAME,
            content_type=DEFAULT_CONTENT_TYPE, description=DEFAULT_DESCRIPTION,
            hash_type=FILE_HASH_TYPE_MD5):
        self.data = data
        self.size = len(self.data)
        self.name = name

        self.content_type = content_type
        self.description = description
        self.date = 0

        self.compute_hash(hash_type)

    def compute_hash(self, hash_type):
        assert hash_type == FILE_HASH_TYPE_MD5

        self.hash_type = hash_type
        m = md5.new()
        m.update(self.data)
        self.hash = m.hexdigest()

class FileTransferTest(object):
    CONTACT_NAME = 'test-ft'

    def __init__(self):
        self.file = File()

    def connect(self):
        self.conn.Connect()
        self.q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

        self.self_handle = self.conn.GetSelfHandle()
        self.self_handle_name =  self.conn.InspectHandles(HT_CONTACT, [self.self_handle])[0]

    def announce_contact(self, name=CONTACT_NAME):
        basic_txt = { "txtvers": "1", "status": "avail" }

        self.contact_name = '%s@%s' % (name, get_host_name())
        self.listener, port = setup_stream_listener(self.q, self.contact_name)

        self.contact_service = AvahiAnnouncer(self.contact_name, "_presence._tcp",
                port, basic_txt)

    def wait_for_contact(self, name=CONTACT_NAME):
        publish_handle = self.conn.RequestHandles(HT_CONTACT_LIST, ["publish"])[0]
        publish = self.conn.RequestChannel(
                "org.freedesktop.Telepathy.Channel.Type.ContactList",
                HT_CONTACT_LIST, publish_handle, False)

        self.handle = 0
        # Wait until the record shows up in publish
        while self.handle == 0:
            e = self.q.expect('dbus-signal', signal='MembersChanged', path=publish)
            for h in e.args[1]:
                name = self.conn.InspectHandles(HT_CONTACT, [h])[0]
                if name == self.contact_name:
                    self.handle = h

    def create_ft_channel(self):
        self.channel = make_channel_proxy(self.conn, self.ft_path, 'Channel')
        self.ft_channel = make_channel_proxy(self.conn, self.ft_path, 'Channel.Type.FileTransfer.DRAFT')
        self.ft_props = dbus.Interface(self.bus.get_object(
            self.conn.object.bus_name, self.ft_path), PROPERTIES_IFACE)

    def close_channel(self):
        self.channel.Close()
        self.q.expect('dbus-signal', signal='Closed')

    def test(self, q, bus, conn):
        self.q = q
        self.bus = bus
        self.conn = conn

        for fct in self._actions:
            # stop if a function returns True
            if fct():
                break

class ReceiveFileTest(FileTransferTest):
    def __init__(self):
        FileTransferTest.__init__(self)

        self._actions = [self.connect, self.announce_contact, self.wait_for_contact,
            self.connect_to_salut, self.setup_http_server, self.send_ft_offer_iq,
            self.check_new_channel, self.create_ft_channel, self.accept_file,
            self.receive_file, self.close_channel]

    def _resolve_salut_presence(self):
        AvahiListener(self.q).listen_for_service("_presence._tcp")
        e = self.q.expect('service-added', name = self.self_handle_name,
            protocol = avahi.PROTO_INET)
        service = e.service
        service.resolve()

        e = self.q.expect('service-resolved', service = service)
        return str(e.pt), e.port

    def connect_to_salut(self):
        host, port = self._resolve_salut_presence()

        self.outbound = connect_to_stream(self.q, self.contact_name,
            self.self_handle_name, host, port)

        e = self.q.expect('connection-result')
        assert e.succeeded, e.reason
        self.q.expect('stream-opened', connection = self.outbound)

    def setup_http_server(self):
        class HTTPHandler(BaseHTTPServer.BaseHTTPRequestHandler):
            def do_GET(self_):
                # is that the right file ?
                filename = self_.path.rsplit('/', 2)[-1]
                assert filename == urllib.quote(self.file.name)

                self_.send_response(200)
                self_.send_header('Content-type', self.file.content_type)
                self_.end_headers()
                self_.wfile.write(self.file.data)

        self.httpd = BaseHTTPServer.HTTPServer(('', 0), HTTPHandler)

    def send_ft_offer_iq(self):
        iq = domish.Element((None, 'iq'))
        iq['to'] = self.self_handle_name
        iq['from'] = self.contact_name
        iq['type'] = 'set'
        iq['id'] = 'gibber-file-transfer-0'
        query = iq.addElement(('jabber:iq:oob', 'query'))
        url = 'http://127.0.0.1:%u/gibber-file-transfer-0/%s' % \
            (self.httpd.server_port, urllib.quote(self.file.name))
        url_node = query.addElement('url', content=url)
        url_node['type'] = 'file'
        url_node['size'] = str(self.file.size)
        url_node['mimeType'] = self.file.content_type
        query.addElement('desc', content=self.file.description)
        self.outbound.send(iq)

    def check_new_channel(self):
        e = self.q.expect('dbus-signal', signal='NewChannels')
        channels = e.args[0]
        assert len(channels) == 1
        path, props = channels[0]

        # check channel properties
        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[CHANNEL_INTERFACE + '.ChannelType'] == CHANNEL_TYPE_FILE_TRANSFER
        assert props[CHANNEL_INTERFACE + '.Interfaces'] == []
        assert props[CHANNEL_INTERFACE + '.TargetHandle'] == self.handle
        assert props[CHANNEL_INTERFACE + '.TargetID'] == self.contact_name
        assert props[CHANNEL_INTERFACE + '.TargetHandleType'] == HT_CONTACT
        assert props[CHANNEL_INTERFACE + '.Requested'] == False
        assert props[CHANNEL_INTERFACE + '.InitiatorHandle'] == self.handle
        assert props[CHANNEL_INTERFACE + '.InitiatorID'] == self.contact_name

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.State'] == FT_STATE_PENDING
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentType'] == self.file.content_type
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Filename'] == self.file.name
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Size'] == self.file.size
        # FT's protocol doesn't allow us the send the hash info
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType'] == FILE_HASH_TYPE_NONE
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash'] == ''
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Description'] == self.file.description
        # FT's protocol doesn't allow us the send the date info
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Date'] == 0
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.AvailableSocketTypes'] == \
            {SOCKET_ADDRESS_TYPE_UNIX: [SOCKET_ACCESS_CONTROL_LOCALHOST]}
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.TransferredBytes'] == 0
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset'] == 0

        self.ft_path = path

    def accept_file(self):
        self.address = self.ft_channel.AcceptFile(SOCKET_ADDRESS_TYPE_UNIX,
                SOCKET_ACCESS_CONTROL_LOCALHOST, "", 5)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_ACCEPTED
        assert reason == FT_STATE_CHANGE_REASON_REQUESTED

        e = self.q.expect('dbus-signal', signal='InitialOffsetDefined')
        offset = e.args[0]
        # We don't support resume
        assert offset == 0

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_OPEN
        assert reason == FT_STATE_CHANGE_REASON_NONE

    def _read_file_from_socket(self, s):
        # Read the file from Salut's socket
        data = ''
        read = 0
        while read < self.file.size:
            data += s.recv(self.file.size - read)
            read = len(data)
        assert data == self.file.data

        e = self.q.expect('dbus-signal', signal='TransferredBytesChanged')
        count = e.args[0]
        while count < self.file.size:
            # Catch TransferredBytesChanged until we transfered all the data
            e = self.q.expect('dbus-signal', signal='TransferredBytesChanged')
            count = e.args[0]

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_COMPLETED
        assert reason == FT_STATE_CHANGE_REASON_NONE

    def receive_file(self):
        # Connect to Salut's socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)

        self.httpd.handle_request()

        # Receiver inform us he finished to download the file
        self.q.expect('stream-iq', iq_type='result')

        self._read_file_from_socket(s)

class SendFileTest(FileTransferTest):
    def __init__(self):
        FileTransferTest.__init__(self)

        self._actions = [self.connect, self.announce_contact, self.wait_for_contact,
            self.check_ft_available, self.request_ft_channel, self.create_ft_channel,
            self.got_send_iq, self.provide_file, self.client_request_file, self.send_file,
            self.close_channel]

    def check_ft_available(self):
        properties = self.conn.GetAll(
                CONNECTION_INTERFACE_REQUESTS,
                dbus_interface=PROPERTIES_IFACE)

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

    def request_ft_channel(self):
        requests_iface = dbus.Interface(self.conn, CONNECTION_INTERFACE_REQUESTS)

        self.ft_path, props = requests_iface.CreateChannel({
            CHANNEL_INTERFACE + '.ChannelType': CHANNEL_TYPE_FILE_TRANSFER,
            CHANNEL_INTERFACE + '.TargetHandleType': HT_CONTACT,
            CHANNEL_INTERFACE + '.TargetHandle': self.handle,
            CHANNEL_TYPE_FILE_TRANSFER + '.ContentType': self.file.content_type,
            CHANNEL_TYPE_FILE_TRANSFER + '.Filename': self.file.name,
            CHANNEL_TYPE_FILE_TRANSFER + '.Size': self.file.size,
            CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType': self.file.hash_type,
            CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash': self.file.hash,
            CHANNEL_TYPE_FILE_TRANSFER + '.Description': self.file.description,
            CHANNEL_TYPE_FILE_TRANSFER + '.Date':  self.file.date,
            CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset': 0,
            })

        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[CHANNEL_INTERFACE + '.ChannelType'] == CHANNEL_TYPE_FILE_TRANSFER
        assert props[CHANNEL_INTERFACE + '.Interfaces'] == []
        assert props[CHANNEL_INTERFACE + '.TargetHandle'] == self.handle
        assert props[CHANNEL_INTERFACE + '.TargetID'] == self.contact_name
        assert props[CHANNEL_INTERFACE + '.TargetHandleType'] == HT_CONTACT
        assert props[CHANNEL_INTERFACE + '.Requested'] == True
        assert props[CHANNEL_INTERFACE + '.InitiatorHandle'] == self.self_handle
        assert props[CHANNEL_INTERFACE + '.InitiatorID'] == self.self_handle_name

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.State'] == FT_STATE_PENDING
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentType'] == self.file.content_type
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Filename'] == self.file.name
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Size'] == self.file.size
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType'] == self.file.hash_type
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash'] == self.file.hash
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Description'] == self.file.description
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Date'] == self.file.date
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.AvailableSocketTypes'] == \
            {SOCKET_ADDRESS_TYPE_UNIX: [SOCKET_ACCESS_CONTROL_LOCALHOST]}
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.TransferredBytes'] == 0
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset'] == 0

    def got_send_iq(self):
        conn_event, iq_event = self.q.expect_many(
            EventPattern('incoming-connection', listener = self.listener),
            EventPattern('stream-iq'))

        self.incoming = conn_event.connection

        self._check_oob_iq(iq_event)

    def _check_oob_iq(self, iq_event):
        assert iq_event.iq_type == 'set'
        assert iq_event.connection == self.incoming
        self.iq = iq_event.stanza
        assert self.iq['to'] == self.contact_name
        query = self.iq.firstChildElement()
        assert query.uri == 'jabber:iq:oob'
        url_node = xpath.queryForNodes("/iq/query/url", self.iq)[0]
        assert url_node['type'] == 'file'
        assert url_node['size'] == str(self.file.size)
        assert url_node['mimeType'] == self.file.content_type
        self.url = url_node.children[0]
        _, self.host, self.filename, _, _, _ = urlparse.urlparse(self.url)
        urllib.unquote(self.filename) == self.file.name
        desc_node = xpath.queryForNodes("/iq/query/desc", self.iq)[0]
        self.desc = desc_node.children[0]
        assert self.desc == self.file.description

    def provide_file(self):
        self.address = self.ft_channel.ProvideFile(SOCKET_ADDRESS_TYPE_UNIX,
                SOCKET_ACCESS_CONTROL_LOCALHOST, "")

    def client_request_file(self):
        # Connect HTTP client to the CM and request the file
        self.http = httplib.HTTPConnection(self.host)
        self.http.request('GET', self.filename)

    def _get_http_response(self):
        response = self.http.getresponse()
        assert (response.status, response.reason) == (200, 'OK')
        data = response.read(self.file.size)
        # Did we received the right file?
        assert data == self.file.data

    def send_file(self):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)
        s.send(self.file.data)

        e = self.q.expect('dbus-signal', signal='TransferredBytesChanged')

        count = e.args[0]
        while count < self.file.size:
            # Catch TransferredBytesChanged until we transfered all the data
            e = self.q.expect('dbus-signal', signal='TransferredBytesChanged')
            count = e.args[0]

        self._get_http_response()

        # Inform sender that we received all the file from the OOB transfer
        reply = domish.Element(('', 'iq'))
        reply['to'] = self.iq['from']
        reply['from'] = self.iq['to']
        reply['type'] = 'result'
        reply['id'] = self.iq['id']
        self.incoming.send(reply)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_COMPLETED
        assert reason == FT_STATE_CHANGE_REASON_NONE
