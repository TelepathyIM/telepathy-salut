from saluttest import exec_test
from xmppstream import IncomingXmppiChatStream, setup_stream_listener
from twisted.words.xish import domish
from avahitest import get_host_name, AvahiAnnouncer

from file_transfer_helper import SendFileTest, FT_STATE_CANCELLED, \
    FT_STATE_CHANGE_REASON_REMOTE_STOPPED, CHANNEL_TYPE_FILE_TRANSFER

class IChatSendFileDeclined(SendFileTest):
    CONTACT_NAME = 'test-ft'

    def announce_contact(self, name=CONTACT_NAME):
        basic_txt = { "txtvers": "1", "status": "avail" }

        self.contact_name = '%s@%s' % (name, get_host_name())
        self.listener, port = setup_stream_listener(self.q, self.contact_name,
                protocol=IncomingXmppiChatStream)

        AvahiAnnouncer(self.contact_name, "_presence._tcp", port, basic_txt)

    def got_send_iq(self):
        SendFileTest.got_send_iq(self)

        # Receiver declines the file offer
        reply = domish.Element(('', 'iq'))
        reply['to'] = self.iq['from']
        reply['type'] = 'error'
        reply['id'] = self.iq['id']
        error_node = reply.addElement((None, 'error'), content='User declined to receive the file')
        error_node['type'] = '406'
        self.incoming.send(reply)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_CANCELLED, state
        assert reason == FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        transferred = self.ft_props.Get(CHANNEL_TYPE_FILE_TRANSFER, 'TransferredBytes')
        # no byte has been transferred as the file was declined
        assert transferred == 0

        self.channel.Close()
        self.q.expect('dbus-signal', signal='Closed')

        # stop test
        return True

if __name__ == '__main__':
    test = IChatSendFileDeclined()
    exec_test(test.test)