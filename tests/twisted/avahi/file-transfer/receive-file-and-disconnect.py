import socket

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest

class ReceiveFileAndDisconnectTest(ReceiveFileTest):
    def receive_file(self):
        s = socket.socket(self._get_socket_address_family(), socket.SOCK_STREAM)
        s.connect(self.address)

        # disconnect
        self.conn.Disconnect()
        self.q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
        return True

if __name__ == '__main__':
    test = ReceiveFileAndDisconnectTest()
    exec_test(test.test)
