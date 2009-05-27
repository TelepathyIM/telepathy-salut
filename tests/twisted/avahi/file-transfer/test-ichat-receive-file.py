import urllib
from saluttest import exec_test
from xmppstream import connect_to_stream, OutgoingXmppiChatStream
from twisted.words.xish import domish

from file_transfer_helper import ReceiveFileTest

print "FIXME: This test fails if there is another LL XMPP instance running on the machine."
# exiting 77 causes automake to consider the test to have been skipped
raise SystemExit(77)

class IChatReceiveFile(ReceiveFileTest):
    def connect_to_salut(self):
        host, port = self._resolve_salut_presence()

        self.outbound = connect_to_stream(self.q, self.contact_name,
            self.self_handle_name, host, port, OutgoingXmppiChatStream)

        e = self.q.expect('connection-result')
        assert e.succeeded, e.reason
        self.q.expect('stream-opened', connection = self.outbound)

    def send_ft_offer_iq(self):
        # connected to Salut, now send the IQ
        iq = domish.Element((None, 'iq'))
        iq['to'] = self.self_handle_name
        # no 'from' attribute
        iq['type'] = 'set'
        iq['id'] = 'iChat_A1FB5D95'
        query = iq.addElement(('jabber:iq:oob', 'query'))
        url = 'http://127.0.0.1:%u/gibber-file-transfer-0/%s' % (self.httpd.server_port,
            urllib.quote(self.file.name))
        url_node = query.addElement('url', content="\n%s" % url)  #iChat adds a \n before the URL
        url_node['type'] = 'file'
        url_node['size'] = str(self.file.size)
        url_node['mimeType'] = self.file.content_type
        url_node['posixflags'] = '00000180'
        query.addElement('desc', content=self.file.description)
        self.outbound.send(iq)

if __name__ == '__main__':
    test = IChatReceiveFile()
    exec_test(test.test)
