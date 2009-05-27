import dbus
import socket

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest, SOCKET_ADDRESS_TYPE_UNIX,\
    SOCKET_ACCESS_CONTROL_LOCALHOST

class ReceiveFileAndSenderDisconnectWhileTransfering(ReceiveFileTest):
    def accept_file(self):
        ReceiveFileTest.accept_file(self)

        # The sender of the file disconnects
        self.outbound.transport.loseConnection()
        self.contact_service.stop()
        # we continue the transfer as it was already accepted

    def receive_file(self):
        # Connect to Salut's socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)

        self.httpd.handle_request()

        # Salut doesn't send the IQ reply as the XMPP connection was broken

        self._read_file_from_socket(s)

if __name__ == '__main__':
    test = ReceiveFileAndSenderDisconnectWhileTransfering()
    exec_test(test.test)
