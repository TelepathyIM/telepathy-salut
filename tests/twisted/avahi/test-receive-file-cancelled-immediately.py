import socket

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTransferTest, FT_STATE_CANCELLED, \
    FT_STATE_CHANGE_REASON_REMOTE_STOPPED

class ReceiveFileCancelledImmediatelyTest(ReceiveFileTransferTest):
    def accept_file(self):
        # sender cancels FT immediately so stop to listen to the HTTP socket
        # before we accept the transfer.
        self.httpd.server_close()

        ReceiveFileTransferTest.accept_file(self)

    def receive_file(self):
        # Connect to Salut's socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)

        # Salut can't connect to download the file
        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_CANCELLED
        assert reason == FT_STATE_CHANGE_REASON_REMOTE_STOPPED

if __name__ == '__main__':
    test = ReceiveFileCancelledImmediatelyTest()
    exec_test(test.test)
