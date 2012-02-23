from saluttest import exec_test
from file_transfer_helper import SendFileTest
import constants as cs

class SendFileTransferIPv4(SendFileTest):
    def __init__(self):
        SendFileTest.__init__(self, cs.SOCKET_ADDRESS_TYPE_IPV4)

    def provide_file(self):
        SendFileTest.provide_file(self)
        state = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'State')
        assert state == cs.FT_STATE_PENDING

    def client_request_file(self):
        SendFileTest.client_request_file(self)
        e = self.q.expect('dbus-signal', signal='InitialOffsetDefined')
        offset = e.args[0]
        # We don't support resume
        assert offset == 0

        # Channel is open. We can start to send the file
        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_OPEN
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

if __name__ == '__main__':
    test = SendFileTransferIPv4()
    exec_test(test.test)
