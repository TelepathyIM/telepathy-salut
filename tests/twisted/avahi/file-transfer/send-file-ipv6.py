import sys

from saluttest import exec_test
from file_transfer_helper import SendFileTest
import constants as cs

if not check_ipv6_enabled():
    print "Skipped test as IPv6 doesn't seem to be available"
    # exiting 77 causes automake to consider the test to have been skipped
    raise SystemExit(77)

if sys.version_info < (2, 7, 1):
    print "FIXME: disabled because of a bug in Python's httplib. http://bugs.python.org/issue5111"
    raise SystemExit(77)

class SendFileTransferIPv6(SendFileTest):
    def __init__(self):
        SendFileTest.__init__(self, cs.SOCKET_ADDRESS_TYPE_IPV6)

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
    test = SendFileTransferIPv6()
    exec_test(test.test)
