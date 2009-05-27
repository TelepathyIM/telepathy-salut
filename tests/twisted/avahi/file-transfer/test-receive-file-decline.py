from servicetest import make_channel_proxy
from saluttest import exec_test

from file_transfer_helper import ReceiveFileTest, FT_STATE_CANCELLED, \
    FT_STATE_CHANGE_REASON_LOCAL_STOPPED

class ReceiveFileDeclineTest(ReceiveFileTest):
    def accept_file(self):
        # decline FT
        self. channel.Close()

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_CANCELLED
        assert reason == FT_STATE_CHANGE_REASON_LOCAL_STOPPED
        self.q.expect('dbus-signal', signal='Closed')

        # Re send offer (this is a regression test as Salut used to crash at this
        # point)
        self.send_ft_offer_iq()

        e = self.q.expect('dbus-signal', signal='NewChannels')
        channels = e.args[0]
        assert len(channels) == 1
        path, props = channels[0]

        channel = make_channel_proxy(self.conn, path, 'Channel')

        # decline FT
        channel.Close()

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_CANCELLED
        assert reason == FT_STATE_CHANGE_REASON_LOCAL_STOPPED
        self.q.expect('dbus-signal', signal='Closed')

        # stop test
        return True

if __name__ == '__main__':
    test = ReceiveFileDeclineTest()
    exec_test(test.test)
