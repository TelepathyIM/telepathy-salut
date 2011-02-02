import dbus
import socket
import hashlib
import avahi
import BaseHTTPServer
import urllib
import httplib
import urlparse

from avahitest import AvahiAnnouncer, AvahiListener, get_host_name
from saluttest import wait_for_contact_in_publish

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, EventPattern
import constants as cs

from twisted.words.xish import domish, xpath

from dbus import PROPERTIES_IFACE

class File(object):
    DEFAULT_DATA = "What a nice file"
    DEFAULT_NAME = "The foo.txt"
    DEFAULT_CONTENT_TYPE = 'text/plain'
    DEFAULT_DESCRIPTION = "A nice file to test"

    def __init__(self, data=DEFAULT_DATA, name=DEFAULT_NAME,
            content_type=DEFAULT_CONTENT_TYPE, description=DEFAULT_DESCRIPTION,
            hash_type=cs.FILE_HASH_TYPE_MD5):
        self.data = data
        self.size = len(self.data)
        self.name = name

        self.content_type = content_type
        self.description = description
        self.date = 0

        self.compute_hash(hash_type)

    def compute_hash(self, hash_type):
        assert hash_type == cs.FILE_HASH_TYPE_MD5

        self.hash_type = hash_type
        self.hash = hashlib.md5(self.data).hexdigest()

class FileTransferTest(object):
    CONTACT_NAME = 'test-ft'

    def __init__(self):
        self.file = File()

    def connect(self):
        self.conn.Connect()
        self.q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

        self.self_handle = self.conn.GetSelfHandle()
        self.self_handle_name =  self.conn.InspectHandles(cs.HT_CONTACT, [self.self_handle])[0]

    def announce_contact(self, name=CONTACT_NAME):
        basic_txt = { "txtvers": "1", "status": "avail" }

        self.contact_name = '%s@%s' % (name, get_host_name())
        self.listener, port = setup_stream_listener(self.q, self.contact_name)

        self.contact_service = AvahiAnnouncer(self.contact_name, "_presence._tcp",
                port, basic_txt)

    def wait_for_contact(self):
        self.handle = wait_for_contact_in_publish(self.q, self.bus, self.conn,
                self.contact_name)

    def create_ft_channel(self):
        self.channel = make_channel_proxy(self.conn, self.ft_path, 'Channel')
        self.ft_channel = make_channel_proxy(self.conn, self.ft_path, 'Channel.Type.FileTransfer')
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

        self.httpd = self._get_http_server_class()(('', 0), HTTPHandler)

    def _get_http_server_class(self):
        return BaseHTTPServer.HTTPServer

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
        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER
        assert props[cs.INTERFACES] == []
        assert props[cs.TARGET_HANDLE] == self.handle
        assert props[cs.TARGET_ID] == self.contact_name
        assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
        assert props[cs.REQUESTED] == False
        assert props[cs.INITIATOR_HANDLE] == self.handle
        assert props[cs.INITIATOR_ID] == self.contact_name

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[cs.FT_STATE] == cs.FT_STATE_PENDING
        assert props[cs.FT_CONTENT_TYPE] == self.file.content_type
        assert props[cs.FT_FILENAME] == self.file.name
        assert props[cs.FT_SIZE] == self.file.size
        # FT's protocol doesn't allow us the send the hash info
        assert props[cs.FT_CONTENT_HASH_TYPE] == cs.FILE_HASH_TYPE_NONE
        assert props[cs.FT_CONTENT_HASH] == ''
        assert props[cs.FT_DESCRIPTION] == self.file.description
        # FT's protocol doesn't allow us the send the date info
        assert props[cs.FT_DATE] == 0
        assert props[cs.FT_AVAILABLE_SOCKET_TYPES] == \
            {cs.SOCKET_ADDRESS_TYPE_UNIX: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]}
        assert props[cs.FT_TRANSFERRED_BYTES] == 0
        assert props[cs.FT_INITIAL_OFFSET] == 0

        self.ft_path = path

    def accept_file(self):
        self.address = self.ft_channel.AcceptFile(cs.SOCKET_ADDRESS_TYPE_UNIX,
                cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "", 5, byte_arrays=True)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_ACCEPTED
        assert reason == cs.FT_STATE_CHANGE_REASON_REQUESTED

        e = self.q.expect('dbus-signal', signal='InitialOffsetDefined')
        offset = e.args[0]
        # We don't support resume
        assert offset == 0

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_OPEN
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

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
        assert state == cs.FT_STATE_COMPLETED
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

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
        properties = self.conn.GetAll(cs.CONN_IFACE_REQUESTS,
                dbus_interface=PROPERTIES_IFACE)

        assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
                 cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT},
                [cs.FT_CONTENT_HASH_TYPE,
                 cs.TARGET_HANDLE,
                 cs.TARGET_ID,
                 cs.FT_CONTENT_TYPE,
                 cs.FT_FILENAME,
                 cs.FT_SIZE,
                 cs.FT_CONTENT_HASH,
                 cs.FT_DESCRIPTION,
                 cs.FT_DATE,
                 cs.FT_INITIAL_OFFSET,
                 cs.FT_URI],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    def request_ft_channel(self):
        requests_iface = dbus.Interface(self.conn, cs.CONN_IFACE_REQUESTS)

        self.ft_path, props = requests_iface.CreateChannel({
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_HANDLE: self.handle,

            cs.FT_CONTENT_TYPE: self.file.content_type,
            cs.FT_FILENAME: self.file.name,
            cs.FT_SIZE: self.file.size,
            cs.FT_CONTENT_HASH_TYPE: self.file.hash_type,
            cs.FT_CONTENT_HASH:self.file.hash,
            cs.FT_DESCRIPTION: self.file.description,
            cs.FT_DATE: self.file.date,
            cs.FT_INITIAL_OFFSET: 0,
            })

        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER
        assert props[cs.INTERFACES] == []
        assert props[cs.TARGET_HANDLE] == self.handle
        assert props[cs.TARGET_ID] == self.contact_name
        assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
        assert props[cs.REQUESTED] == True
        assert props[cs.INITIATOR_HANDLE] == self.self_handle
        assert props[cs.INITIATOR_ID] == self.self_handle_name

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[cs.FT_STATE] == cs.FT_STATE_PENDING
        assert props[cs.FT_CONTENT_TYPE] == self.file.content_type
        assert props[cs.FT_FILENAME] == self.file.name
        assert props[cs.FT_SIZE] == self.file.size
        assert props[cs.FT_CONTENT_HASH_TYPE] == self.file.hash_type
        assert props[cs.FT_CONTENT_HASH] == self.file.hash
        assert props[cs.FT_DESCRIPTION] == self.file.description
        assert props[cs.FT_DATE] == self.file.date
        assert props[cs.FT_AVAILABLE_SOCKET_TYPES] == \
            {cs.SOCKET_ADDRESS_TYPE_UNIX: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]}
        assert props[cs.FT_TRANSFERRED_BYTES] == 0
        assert props[cs.FT_INITIAL_OFFSET] == 0

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
        self.address = self.ft_channel.ProvideFile(cs.SOCKET_ADDRESS_TYPE_UNIX,
                cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "", byte_arrays=True)

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
        assert state == cs.FT_STATE_COMPLETED
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE
