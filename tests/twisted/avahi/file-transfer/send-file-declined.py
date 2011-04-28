from saluttest import exec_test
from file_transfer_helper import SendFileTest

from twisted.words.xish import domish
import constants as cs

class SendFileDeclinedTest(SendFileTest):
    def got_send_iq(self):
        SendFileTest.got_send_iq(self)

        # Receiver declines the file offer
        reply = domish.Element(('', 'iq'))
        reply['to'] = self.iq['from']
        reply['from'] = self.iq['to']
        reply['type'] = 'error'
        reply['id'] = self.iq['id']
        query = reply.addElement(('jabber:iq:oob', 'query'))
        url_node = query.addElement('url', content=self.url)
        query.addElement('desc', content=self.desc)
        error_node = reply.addElement((None, 'error'))
        error_node['code'] = '406'
        error_node['type'] = 'modify'
        not_acceptable_node = error_node.addElement(('urn:ietf:params:xml:ns:xmpp-stanzas',
            'not-acceptable'))
        self.incoming.send(reply)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_CANCELLED, state
        assert reason == cs.FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        transferred = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'TransferredBytes')
        # no byte has been transferred as the file was declined
        assert transferred == 0

        # stop test
        return True

if __name__ == '__main__':
    test = SendFileDeclinedTest()
    exec_test(test.test)
