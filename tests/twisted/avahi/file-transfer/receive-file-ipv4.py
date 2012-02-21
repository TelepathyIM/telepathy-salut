import avahi
import urllib
import BaseHTTPServer
import SocketServer
import socket

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest

from avahitest import AvahiAnnouncer, get_host_name, AvahiListener
from xmppstream import connect_to_stream, setup_stream_listener

from twisted.words.xish import domish

class TestReceiveFileIPv4(ReceiveFileTest):
    CONTACT_NAME = 'test-ft'

    service_name = ''
    metadata = {}

    def announce_contact(self, name=CONTACT_NAME):
        basic_txt = { "txtvers": "1", "status": "avail" }

        self.contact_name = '%s@%s' % (name, get_host_name())
        self.listener, port = setup_stream_listener(self.q, self.contact_name)

        self.contact_service = AvahiAnnouncer(self.contact_name, "_presence._tcp", port,
                basic_txt, proto=avahi.PROTO_INET)


    def _resolve_salut_presence(self):
        AvahiListener(self.q).listen_for_service("_presence._tcp")
        e = self.q.expect('service-added', name = self.self_handle_name,
            protocol = avahi.PROTO_INET)
        service = e.service
        service.resolve()

        e = self.q.expect('service-resolved', service = service,
                          protocol = avahi.PROTO_INET)
        return str(e.pt), e.port

    def connect_to_salut(self):
        host, port = self._resolve_salut_presence()

        self.outbound = connect_to_stream(self.q, self.contact_name,
            self.self_handle_name, host, port)

        e = self.q.expect('connection-result')
        assert e.succeeded, e.reason
        self.q.expect('stream-opened', connection = self.outbound)

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

    def _get_http_server_class(self):
        class HTTPServer(SocketServer.ThreadingMixIn, BaseHTTPServer.HTTPServer):
            address_family = getattr(socket, 'AF_INET', None)

        return HTTPServer

if __name__ == '__main__':
    test = TestReceiveFileIPv4()
    exec_test(test.test)
