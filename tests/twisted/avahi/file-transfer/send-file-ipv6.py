import avahi
from saluttest import exec_test
from avahitest import AvahiAnnouncer, get_host_name
from xmppstream import setup_stream_listener6
from file_transfer_helper import SendFileTest
import socket

import constants as cs

print "FIXME: disabled because of a bug in Python's httplib. http://bugs.python.org/issue5111"
# exiting 77 causes automake to consider the test to have been skipped
raise SystemExit(77)

class SendFileTransferIPv6(SendFileTest):
    CONTACT_NAME = 'test-ft'

    def announce_contact(self, name=CONTACT_NAME):
        basic_txt = { "txtvers": "1", "status": "avail" }

        self.contact_name = '%s@%s' % (name, get_host_name())
        self.listener, port = setup_stream_listener6(self.q, self.contact_name)

        self.contact_service = AvahiAnnouncer(self.contact_name, "_presence._tcp", port,
                basic_txt, proto=avahi.PROTO_INET6)

    def provide_file(self):
        SendFileTest.provide_file(self, cs.SOCKET_ADDRESS_TYPE_IPV6)

        # state is still Pending as remote didn't accept the transfer yet
        state = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'State')
        assert state == cs.FT_STATE_PENDING

    def send_file(self):
      SendFileTest.send_file(socket.AF_INET6);

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
