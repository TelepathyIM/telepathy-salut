from saluttest import exec_test
from xmppstream import IncomingXmppiChatStream, setup_stream_listener
from twisted.words.xish import domish
from avahitest import get_host_name, AvahiAnnouncer

from file_transfer_helper import SendFileTest
import constants as cs

class IChatSendFileDeclined(SendFileTest):
    CONTACT_NAME = 'test-ft'

    # we need to unset these so we won't try and send them and then
    # because we don't have the right caps, salut complains
    service_name = ''
    metadata = {}

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
        assert state == cs.FT_STATE_CANCELLED, state
        assert reason == cs.FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        transferred = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'TransferredBytes')
        # no byte has been transferred as the file was declined
        assert transferred == 0

        self.channel.Close()
        self.q.expect('dbus-signal', signal='Closed')

        # stop test
        return True

if __name__ == '__main__':
    test = IChatSendFileDeclined()
    exec_test(test.test)
