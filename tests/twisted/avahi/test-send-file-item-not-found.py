from saluttest import exec_test
from file_transfer_helper import SendFileTest, FT_STATE_CANCELLED, \
    FT_STATE_CHANGE_REASON_REMOTE_ERROR, CHANNEL_TYPE_FILE_TRANSFER

from twisted.words.xish import domish

class SendFileItemNotFound(SendFileTest):
    def client_request_file(self):
        # Receiver can't retrieve the file
        reply = domish.Element(('', 'iq'))
        reply['to'] = self.iq['from']
        reply['from'] = self.iq['to']
        reply['type'] = 'error'
        reply['id'] = self.iq['id']
        query = reply.addElement(('jabber:iq:oob', 'query'))
        url_node = query.addElement('url', content=self.url)
        query.addElement('desc', content=self.desc)
        error_node = reply.addElement((None, 'error'))
        error_node['code'] = '404'
        error_node['type'] = 'modify'
        error_node.addElement(('urn:ietf:params:xml:ns:xmpp-stanzas',
            'item-not-found'))
        self.incoming.send(reply)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_CANCELLED, state
        assert reason == FT_STATE_CHANGE_REASON_REMOTE_ERROR

        transferred = self.ft_props.Get(CHANNEL_TYPE_FILE_TRANSFER, 'TransferredBytes')
        # no byte has been transferred as the transfer failed
        assert transferred == 0

        # stop test
        return True

if __name__ == '__main__':
    test = SendFileItemNotFound()
    exec_test(test.test)
