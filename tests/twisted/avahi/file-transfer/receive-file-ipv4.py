from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest
import constants as cs

class TestReceiveFileIPv4(ReceiveFileTest):
    def __init__(self):
        ReceiveFileTest.__init__(self, cs.SOCKET_ADDRESS_TYPE_IPV4)

if __name__ == '__main__':
    test = TestReceiveFileIPv4()
    exec_test(test.test)
