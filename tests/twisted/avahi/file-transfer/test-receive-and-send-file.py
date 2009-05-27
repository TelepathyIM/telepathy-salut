import urlparse
import urllib

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest, SendFileTest

from twisted.words.xish import xpath

class ReceiveAndSendFileTest(ReceiveFileTest, SendFileTest):
    def __init__(self):
        ReceiveFileTest.__init__(self)
        SendFileTest.__init__(self)

        self._actions = [self.connect, self.announce_contact,self.wait_for_contact,
                self.connect_to_salut,
                # receive file
                self.setup_http_server, self.send_ft_offer_iq, self.check_new_channel,
                self.create_ft_channel, self.accept_file, self.receive_file,
                self.close_channel,

                # now send a file. We'll reuse the same XMPP connection
               self.request_ft_channel, self.create_ft_channel, self.got_send_iq,
               self.provide_file, self.client_request_file, self.send_file,
               self.close_channel]

    def got_send_iq(self):
        # reuse the existing XMPP connection
        self.incoming = self.outbound

        iq_event = self.q.expect('stream-iq')

        self._check_oob_iq(iq_event)

if __name__ == '__main__':
    test = ReceiveAndSendFileTest()
    exec_test(test.test)
