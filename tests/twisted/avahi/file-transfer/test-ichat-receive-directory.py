from saluttest import exec_test
from avahitest import skip_if_another_llxmpp
from xmppstream import connect_to_stream, OutgoingXmppiChatStream
from twisted.words.xish import domish

from file_transfer_helper import ReceiveFileTest

class IChatReceiveDirectory(ReceiveFileTest):
    def connect_to_salut(self):
        host, port = self._resolve_salut_presence()

        self.outbound = connect_to_stream(self.q, self.contact_name,
            self.self_handle_name, host, port, OutgoingXmppiChatStream)

        e = self.q.expect('connection-result')
        assert e.succeeded, e.reason
        self.q.expect('stream-opened', connection = self.outbound)

    def send_ft_offer_iq(self):
        iq = domish.Element((None, 'iq'))
        iq['to'] = self.self_handle_name
        # no 'from' attribute
        iq['type'] = 'set'
        iq['id'] = 'iChat_A1FB5D95'
        query = iq.addElement(('jabber:iq:oob', 'query'))
        url = 'http://127.0.0.1:%u/gibber-file-transfer-0/my_directory/' % (self.httpd.server_port)
        url_node = query.addElement('url', content=url)
        url_node['type'] = 'directory'
        url_node['size'] = '1000'
        url_node['nfiles'] = '5'
        url_node['posixflags'] = '00000180'
        self.outbound.send(iq)

        # Send an error as we don't support directory transfer for now
        self.q.expect('stream-iq', iq_type='error')

        # stop the test
        return True

if __name__ == '__main__':
    skip_if_another_llxmpp()
    test = IChatReceiveDirectory()
    exec_test(test.test)
