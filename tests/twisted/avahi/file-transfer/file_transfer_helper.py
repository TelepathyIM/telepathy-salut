import dbus
import socket
import hashlib
import avahi
import BaseHTTPServer
import urllib
import httplib
import urlparse
import sys
import os

from avahitest import AvahiAnnouncer, AvahiListener, get_host_name
from saluttest import wait_for_contact_in_publish

from caps_helper import extract_data_forms, add_dataforms, compute_caps_hash, \
    send_disco_reply

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, EventPattern, assertEquals, call_async, sync_dbus
import constants as cs
import ns

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

        self.uri = 'file:///tmp/%s' % self.name

    def compute_hash(self, hash_type):
        assert hash_type == cs.FILE_HASH_TYPE_MD5

        self.hash_type = hash_type
        self.hash = hashlib.md5(self.data).hexdigest()

class FileTransferTest(object):
    CONTACT_NAME = 'test-ft'

    service_name = 'wacky.service.name'
    metadata = {'loads': 'of',
                'mental': 'data'}

    def __init__(self):
        self.file = File()
        self.contact_service = None

    def connect(self):
        self.conn.Connect()
        self.q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

        self.self_handle = self.conn.GetSelfHandle()
        self.self_handle_name =  self.conn.InspectHandles(cs.HT_CONTACT, [self.self_handle])[0]

    def announce_contact(self, name=CONTACT_NAME, metadata=True):
        client = 'http://telepathy.freedesktop.org/fake-client'
        features = [ns.IQ_OOB]

        if metadata:
            features += [ns.TP_FT_METADATA]

        ver = compute_caps_hash([], features, {})
        txt_record = { "txtvers": "1", "status": "avail",
                       "node": client, "ver": ver, "hash": "sha-1"}

        suffix = '@%s' % get_host_name()
        name += ('-' + os.path.splitext(os.path.basename(sys.argv[0]))[0])

        self.contact_name = name + suffix
        if len(self.contact_name) > 63:
            allowed = 63 - len(suffix)
            self.contact_name = name[:allowed] + suffix

        self.listener, port = setup_stream_listener(self.q, self.contact_name)

        self.contact_service = AvahiAnnouncer(self.contact_name, "_presence._tcp",
                port, txt_record)

        self.handle = wait_for_contact_in_publish(self.q, self.bus, self.conn,
                self.contact_name)

        # expect salut to disco our caps
        e = self.q.expect('incoming-connection', listener=self.listener)
        stream = e.connection

        e = self.q.expect('stream-iq', to=self.contact_name, query_ns=ns.DISCO_INFO,
                     connection=stream)
        assertEquals(client + '#' + ver, e.query['node'])
        send_disco_reply(stream, e.stanza, [], features)

        # lose the connection here to ensure connections are created
        # where necessary; I just wanted salut to know my caps.
        stream.send('</stream:stream>')
        # spend a bit of time in the main loop to ensure the last two
        # stanzas are actually received by salut before closing the
        # connection.
        sync_dbus(self.bus, self.q, self.conn)
        stream.transport.loseConnection()

    def wait_for_contact(self):
        if not hasattr(self, 'handle'):
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

        # if we announced the service, let's be sure to get rid of it
        if self.contact_service:
            self.contact_service.stop()

class ReceiveFileTest(FileTransferTest):
    def __init__(self):
        FileTransferTest.__init__(self)

        self._actions = [self.connect, self.announce_contact, self.wait_for_contact,
            self.connect_to_salut, self.setup_http_server, self.send_ft_offer_iq,
            self.check_new_channel, self.create_ft_channel, self.set_uri,
            self.accept_file, self.receive_file, self.close_channel]

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

            def log_message(self, format, *args):
                if 'CHECK_TWISTED_VERBOSE' in os.environ:
                    BaseHTTPServer.BaseHTTPRequestHandler.log_message(self, format, *args)

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

        # Metadata
        if self.service_name:
            service_form = {ns.TP_FT_METADATA_SERVICE: {'ServiceName': [self.service_name]}}
            add_dataforms(query, service_form)

        if self.metadata:
            metadata_form = {ns.TP_FT_METADATA: {k: [v] for k, v in self.metadata.items()}}
            add_dataforms(query, metadata_form)

        self.outbound.send(iq)

    def check_new_channel(self):
        e = self.q.expect('dbus-signal', signal='NewChannels',
            predicate=lambda e:
                e.args[0][0][1][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER)

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

        assertEquals(self.service_name, props[cs.FT_SERVICE_NAME])
        assertEquals(self.metadata, props[cs.FT_METADATA])

        self.ft_path = path

    def set_uri(self):
        ft_props = dbus.Interface(self.ft_channel, cs.PROPERTIES_IFACE)

        # URI is not set yet
        uri = ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI')
        assertEquals('', uri)

        # Setting URI
        call_async(self.q, ft_props, 'Set',
            cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI', self.file.uri)

        self.q.expect('dbus-signal', signal='URIDefined', args=[self.file.uri])

        self.q.expect('dbus-return', method='Set')

        # Check it has the right value now
        uri = ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI')
        assertEquals(self.file.uri, uri)

        # We can't change it once it has been set
        call_async(self.q, ft_props, 'Set',
            cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI', 'badger://snake')
        self.q.expect('dbus-error', method='Set', name=cs.INVALID_ARGUMENT)

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
                 cs.FT_URI,
                 cs.FT_SERVICE_NAME,
                 cs.FT_METADATA],
             ) in properties.get('RequestableChannelClasses', []),\
                     properties.get('RequestableChannelClasses')

    def request_ft_channel(self, uri=True):
        request = { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_HANDLE: self.handle,

            cs.FT_CONTENT_TYPE: self.file.content_type,
            cs.FT_FILENAME: self.file.name,
            cs.FT_SIZE: self.file.size,
            cs.FT_CONTENT_HASH_TYPE: self.file.hash_type,
            cs.FT_CONTENT_HASH:self.file.hash,
            cs.FT_DESCRIPTION: self.file.description,
            cs.FT_DATE: self.file.date,
            cs.FT_INITIAL_OFFSET: 0 }

        if self.service_name:
            request[cs.FT_SERVICE_NAME] = self.service_name
        if self.metadata:
            request[cs.FT_METADATA] = dbus.Dictionary(self.metadata, signature='ss')

        if uri:
            request[cs.FT_URI] = self.file.uri

        self.ft_path, props = self.conn.Requests.CreateChannel(request)

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
        if uri:
            assertEquals(self.file.uri, props[cs.FT_URI])
        else:
            assertEquals('', props[cs.FT_URI])
        assertEquals(self.service_name, props[cs.FT_SERVICE_NAME])
        assertEquals(self.metadata, props[cs.FT_METADATA])

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

        # Metadata forms
        forms = extract_data_forms(xpath.queryForNodes('/iq/query/x', self.iq))

        if self.service_name:
            assertEquals({'ServiceName': [self.service_name]},
                         forms[ns.TP_FT_METADATA_SERVICE])
        else:
            assert ns.TP_FT_METADATA_SERVICE not in forms

        if self.metadata:
            # the dataform isn't such a simple a{ss} because it can
            # have multiple values
            expected = {k:[v] for k,v in self.metadata.items()}
            assertEquals(expected, forms[ns.TP_FT_METADATA])
        else:
            assert ns.TP_FT_METADATA not in forms

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
