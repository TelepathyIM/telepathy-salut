import dbus

from saluttest import exec_test
from file_transfer_helper import SendFileTest, HT_CONTACT

from avahitest import get_host_name
import constants as cs

class SendFileTransferToUnknownContactTest(SendFileTest):
    def __init__(self):
        SendFileTest.__init__(self)

        self._actions = [self.connect, self.check_ft_available, self.my_request_ft_channel]

    def my_request_ft_channel(self):
        self.contact_name = '%s@%s' % (self.CONTACT_NAME, get_host_name())
        self.handle = self.conn.RequestHandles(HT_CONTACT, [self.contact_name])[0]

        try:
            self.request_ft_channel()
        except dbus.DBusException, e:
            if e.get_dbus_name() != cs.NOT_AVAILABLE:
                raise
        else:
            assert False, "Should raise NotAvailable error"

if __name__ == '__main__':
    test = SendFileTransferToUnknownContactTest()
    exec_test(test.test)
