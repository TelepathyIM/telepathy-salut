
"""
Infrastructure code for testing Salut
"""

import os
import sys
import time

import servicetest
from twisted.internet import reactor
import constants as cs
from twisted.words.protocols.jabber.client import IQ
import ns

import dbus

# keep sync with src/salut-capabilities.c:self_advertised_features
fixed_features = [ns.SI, ns.IBB, ns.TUBES, ns.IQ_OOB, ns.X_OOB]

def make_result_iq(iq):
    result = IQ(None, "result")
    result["id"] = iq["id"]
    query = iq.firstChildElement()

    if query:
        result.addElement((query.uri, query.name))

    return result

def sync_stream(q, xmpp_connection):
    """Used to ensure that Salut has processed all stanzas sent to it on this
       xmpp_connection."""

    iq = IQ(None, "get")
    iq.addElement(('http://jabber.org/protocol/disco#info', 'query'))
    xmpp_connection.send(iq)
    q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info')

def make_connection(bus, event_func, params=None):
    default_params = {
        'published-name': 'testsuite',
        'first-name': 'test',
        'last-name': 'suite',
        }

    if params:
        default_params.update(params)

    return servicetest.make_connection(bus, event_func, 'salut',
        'local-xmpp', default_params)

def install_colourer():
    def red(s):
        return '\x1b[31m%s\x1b[0m' % s

    def green(s):
        return '\x1b[32m%s\x1b[0m' % s

    patterns = {
        'handled': green,
        'not handled': red,
        }

    class Colourer:
        def __init__(self, fh, patterns):
            self.fh = fh
            self.patterns = patterns

        def write(self, s):
            f = self.patterns.get(s, lambda x: x)
            self.fh.write(f(s))

    sys.stdout = Colourer(sys.stdout, patterns)
    return sys.stdout


def exec_test_deferred (fun, params, protocol=None, timeout=None):
    colourer = None

    if sys.stdout.isatty():
        colourer = install_colourer()

    queue = servicetest.IteratingEventQueue(timeout)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    bus = dbus.SessionBus()
    conn = make_connection(bus, queue.append, params)
    error = None

    try:
        fun(queue, bus, conn)
    except Exception, e:
        import traceback
        traceback.print_exc()
        error = e

    try:
        if colourer:
          sys.stdout = colourer.fh

        if error is None:
          reactor.callLater(0, reactor.stop)
        else:
          # please ignore the POSIX behind the curtain
          os._exit(1)

        conn.Disconnect()
    except dbus.DBusException, e:
        pass

    if 'SALUT_TEST_REFDBG' in os.environ:
        # we have to wait that Salut timeouts so the process is properly
        # exited and refdbg can generates its report
        time.sleep(5.5)

def exec_test(fun, params=None, protocol=None, timeout=None):
  reactor.callWhenRunning (exec_test_deferred, fun, params, protocol, timeout)
  reactor.run()

def wait_for_contact_list(q, conn):
    """Request contact list channels and wait for their NewChannel signals.
    This is useful to avoid these signals to interfere with your test."""

    #FIXME: this maybe racy if there are other contacts connected
    requestotron = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    # publish
    requestotron.EnsureChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_LIST,
        cs.TARGET_ID: 'publish'})
    q.expect('dbus-signal', signal='NewChannel')
    # subscribe
    requestotron.EnsureChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_LIST,
        cs.TARGET_ID: 'subscribe'})
    q.expect('dbus-signal', signal='NewChannel')
    # known
    requestotron.EnsureChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_LIST,
        cs.TARGET_ID: 'known'})
    q.expect('dbus-signal', signal='NewChannel')
