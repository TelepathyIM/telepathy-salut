# The 'normal' cases are tested with test-receive-file.py and test-send-file-provide-immediately.py
# This file tests some corner cases
import dbus

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest, SendFileTest

import constants as cs

class SendFileNoMetadata(SendFileTest):
    # this is basically the equivalent of calling CreateChannel
    # without these two properties
    service_name = ''
    metadata = {}

class ReceiveFileNoMetadata(ReceiveFileTest):
    service_name = ''
    metadata = {}

if __name__ == '__main__':
    test = SendFileNoMetadata()
    exec_test(test.test)

    test = ReceiveFileNoMetadata()
    exec_test(test.test)
