import socket
import BaseHTTPServer
import urllib

from twisted.words.xish import xpath

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest,FT_STATE_CANCELLED, \
    FT_STATE_CHANGE_REASON_LOCAL_ERROR, CHANNEL_TYPE_FILE_TRANSFER

class ReceiveFileNotFound(ReceiveFileTest):
    def setup_http_server(self):
        class HTTPHandler(BaseHTTPServer.BaseHTTPRequestHandler):
            def do_GET(self_):
                # is that the right file ?
                filename = self_.path.rsplit('/', 2)[-1]
                assert filename == urllib.quote(self.file.name)

                self_.send_response(404)
                self_.end_headers()
                self_.wfile.write(self.file.data)

        self.httpd = BaseHTTPServer.HTTPServer(('', 0), HTTPHandler)

    def receive_file(self):
        # Connect to Salut's socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)

        self.httpd.handle_request()

        # Receiver inform us he can't download the file
        e = self.q.expect('stream-iq', iq_type='error')
        iq = e.stanza
        error_node = xpath.queryForNodes("/iq/error", iq)[0]
        assert error_node['code'] == '404'
        assert error_node['type'] == 'cancel'
        not_found_node = error_node.firstChildElement()
        assert not_found_node.name == 'item-not-found'

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_CANCELLED
        assert reason == FT_STATE_CHANGE_REASON_LOCAL_ERROR

        transferred = self.ft_props.Get(CHANNEL_TYPE_FILE_TRANSFER, 'TransferredBytes')
        # no byte has been transferred as the transfer failed
        assert transferred == 0

if __name__ == '__main__':
    test = ReceiveFileNotFound()
    exec_test(test.test)
