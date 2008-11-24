from saluttest import exec_test
from file_transfer_helper import SendFileTest

class SendFileAndDisconnectTest(SendFileTest):
    def provide_file(self):
        SendFileTest.provide_file(self)

        # regression test: Salut used to crash at this point
        self.conn.Disconnect()
        self.q.expect('dbus-signal', signal='StatusChanged', args=[2L, 1L])

        # stop test
        return True

if __name__ == '__main__':
    test = SendFileAndDisconnectTest()
    exec_test(test.test)
