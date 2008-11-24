import errno
import httplib
import socket

from saluttest import exec_test
from file_transfer_helper import SendFileTest, FT_STATE_CANCELLED, \
    FT_STATE_CHANGE_REASON_LOCAL_STOPPED

class SendFileAndCancelImmediatelyTest(SendFileTest):
    def provide_file(self):
        SendFileTest.provide_file(self)

        # cancel the transfer before the receiver accepts it
        self.channel.Close()

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_CANCELLED
        assert reason == FT_STATE_CHANGE_REASON_LOCAL_STOPPED

        self.q.expect('dbus-signal', signal='Closed')

    def client_request_file(self):
        # Connect HTTP client to the CM and request the file
        http = httplib.HTTPConnection(self.host)
        # can't retry the file as the transfer was cancelled
        try:
            http.request('GET', self.filename)
        except socket.error, e:
            code, msg = e.args
            assert errno.errorcode[code] == 'ECONNREFUSED'
        else:
            assert False

        # stop test
        return True

if __name__ == '__main__':
    test = SendFileAndCancelImmediatelyTest()
    exec_test(test.test)
