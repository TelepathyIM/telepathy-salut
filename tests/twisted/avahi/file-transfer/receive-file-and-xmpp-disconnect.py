import dbus

import socket

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest

class ReceiveFileAndXmppDisconnectTest(ReceiveFileTest):
    def accept_file(self):
        # The XMPP connection is broken
        self.outbound.transport.loseConnection()

        ReceiveFileTest.accept_file(self)

    def receive_file(self):
        # Connect to Salut's socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)

        self.httpd.handle_request()

        # Salut doesn't send the IQ reply as the XMPP connection was broken

        self._read_file_from_socket(s)

if __name__ == '__main__':
    test = ReceiveFileAndXmppDisconnectTest()
    exec_test(test.test)
