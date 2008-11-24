from saluttest import exec_test
from file_transfer_helper import ReceiveFileTransferTest

if __name__ == '__main__':
    test = ReceiveFileTransferTest()
    exec_test(test.test)
