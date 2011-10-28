import httplib
import struct

from saluttest import exec_test
from xmppstream import IncomingXmppiChatStream, setup_stream_listener
from avahitest import get_host_name, AvahiAnnouncer

from file_transfer_helper import SendFileTest

class IChatSendFileDeclined(SendFileTest):
    CONTACT_NAME = 'test-ft'

    # we need to unset these so we won't try and send them and then
    # because we don't have the right caps, salut complains
    service_name = ''
    metadata = {}

    def announce_contact(self, name=CONTACT_NAME):
        basic_txt = { "txtvers": "1", "status": "avail" }

        self.contact_name = '%s@%s' % (name, get_host_name())
        self.listener, port = setup_stream_listener(self.q, self.contact_name,
                protocol=IncomingXmppiChatStream)

        AvahiAnnouncer(self.contact_name, "_presence._tcp", port, basic_txt)

    def client_request_file(self):
        # Connect HTTP client to the CM and request the file
        self.http = httplib.HTTPConnection(self.host)
        headers = {'Accept-Encoding': 'AppleSingle'}
        self.http.request('GET', self.filename, headers=headers)

    def _get_http_response(self):
        # Header is 38 bytes
        APPLE_SINGLE_HEADER_SIZE = 38

        response = self.http.getresponse()
        assert (response.status, response.reason) == (200, 'OK')
        data = response.read(self.file.size + APPLE_SINGLE_HEADER_SIZE)

        # File is using the AppleSingle encoding
        magic_number, version, f1, f2, f3, f4, nb_entry, entry_id, offset, length = struct.unpack(
                '>II4IhIII', data[:APPLE_SINGLE_HEADER_SIZE])

        assert hex(magic_number) == '0x51600'
        assert hex(version) == '0x20000'
        # filler
        assert f1 == f2 == f3 == f4 == 0
        assert nb_entry == 1
        # data fork
        assert entry_id == 1
        assert offset == APPLE_SINGLE_HEADER_SIZE
        assert length == self.file.size

        # Did we received the right file?
        assert data[APPLE_SINGLE_HEADER_SIZE:] == self.file.data

if __name__ == '__main__':
    test = IChatSendFileDeclined()
    exec_test(test.test)
