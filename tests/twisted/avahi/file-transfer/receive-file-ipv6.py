import avahi
import urllib
import BaseHTTPServer
import SocketServer
import socket

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest

from avahitest import AvahiAnnouncer, get_host_name, AvahiListener,\
    check_ipv6_enabled
from xmppstream import connect_to_stream6, setup_stream_listener6

from twisted.words.xish import domish

if not check_ipv6_enabled():
    print "Skipped test as IPv6 doesn't seem to be available"
    # exiting 77 causes automake to consider the test to have been skipped
    raise SystemExit(77)
import constants as cs

class TestReceiveFileIPv6(ReceiveFileTest):
    def __init__(self):
        ReceiveFileTest.__init__(self, cs.SOCKET_ADDRESS_TYPE_IPV6)

if __name__ == '__main__':
    test = TestReceiveFileIPv6()
    exec_test(test.test)
