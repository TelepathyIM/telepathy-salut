import avahi
import urllib
import BaseHTTPServer
import SocketServer
import socket

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest

from avahitest import AvahiAnnouncer, get_host_name, AvahiListener,\
    AvahiService, get_domain_name
from xmppstream import connect_to_stream6, setup_stream_listener6
from servicetest import TimeoutError

from twisted.words.xish import domish

class TestReceiveFileIPv6(ReceiveFileTest):
    CONTACT_NAME = 'test-ft'

    def announce_contact(self, name=CONTACT_NAME):
        basic_txt = { "txtvers": "1", "status": "avail" }

        self.contact_name = '%s@%s' % (name, get_host_name())
        self.listener, port = setup_stream_listener6(self.q, self.contact_name)

        self.contact_service = AvahiAnnouncer(self.contact_name, "_presence._tcp", port,
                basic_txt, proto=avahi.PROTO_INET6)

        # Avahi doesn't complain if we try to announce an IPv6 service with a
        # not IPv6 enabled Avahi (http://www.avahi.org/ticket/264) so we try to
        # resolve our own service to check if it has been actually announced.
        service = AvahiService(self.q, self.contact_service.bus, self.contact_service.server,
            avahi.IF_UNSPEC, self.contact_service.proto, self.contact_service.name,
            self.contact_service.type, get_domain_name(), avahi.PROTO_INET6, 0)
        service.resolve()

        try:
            self.q.expect('service-resolved', service=service)
        except TimeoutError:
            print "skip test as IPv6 doesn't seem to be enabled in Avahi"
            return True

    def _resolve_salut_presence(self):
        AvahiListener(self.q).listen_for_service("_presence._tcp")
        e = self.q.expect('service-added', name = self.self_handle_name,
            protocol = avahi.PROTO_INET6)
        service = e.service
        service.resolve()

        e = self.q.expect('service-resolved', service = service)
        return str(e.pt), e.port

    def connect_to_salut(self):
        host, port = self._resolve_salut_presence()

        self.outbound = connect_to_stream6(self.q, self.contact_name,
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
        url = 'http://[::1]:%u/gibber-file-transfer-0/%s' % \
            (self.httpd.server_port, urllib.quote(self.file.name))
        url_node = query.addElement('url', content=url)
        url_node['type'] = 'file'
        url_node['size'] = str(self.file.size)
        url_node['mimeType'] = self.file.content_type
        query.addElement('desc', content=self.file.description)
        self.outbound.send(iq)

    def _get_http_server_class(self):
        class HTTPServer6(SocketServer.ThreadingMixIn, BaseHTTPServer.HTTPServer):
            address_family = getattr(socket, 'AF_INET6', None)

        return HTTPServer6

if __name__ == '__main__':
    test = TestReceiveFileIPv6()
    exec_test(test.test)
