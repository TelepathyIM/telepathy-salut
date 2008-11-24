from saluttest import exec_test
from file_transfer_helper import SendFileTest

class SendFileAndDisconnectTest(SendFileTest):
    def provide_file(self):
        SendFileTest.provide_file(self)

        # regression test: Salut used to crash at this point
        self.conn.Disconnect()
        self.q.expect('dbus-signal', signal='StatusChanged', args=[2L, 1L])

    def client_request_file(self):
        pass

    def send_file(self):
        pass

    def close_channel(self):
        pass

if __name__ == '__main__':
    test = SendFileAndDisconnectTest()
    exec_test(test.test)
