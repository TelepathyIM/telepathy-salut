import dbus

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest, SOCKET_ADDRESS_TYPE_UNIX,\
    SOCKET_ACCESS_CONTROL_LOCALHOST
import constants as cs

class ReceiveFileAndSenderDisconnectWhilePendingTest(ReceiveFileTest):
    def accept_file(self):
        # The sender of the file disconnects
        self.outbound.transport.loseConnection()
        self.contact_service.stop()

        self.q.expect('dbus-signal', signal='FileTransferStateChanged')

        # We can't accept the transfer now
        try:
            self.ft_channel.AcceptFile(SOCKET_ADDRESS_TYPE_UNIX,
                SOCKET_ACCESS_CONTROL_LOCALHOST, "", 0, byte_arrays=True)
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.NOT_AVAILABLE
        else:
            assert False, "Should raise NotAvailable error"

        self.close_channel()

        # stop the test
        return True

if __name__ == '__main__':
    test = ReceiveFileAndSenderDisconnectWhilePendingTest()
    exec_test(test.test)
