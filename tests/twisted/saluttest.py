
"""
Infrastructure code for testing Salut
"""

import base64
import os
import sha
import sys
import time

import servicetest
import twisted
from twisted.internet import reactor

import dbus

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

    requestotron = dbus.Interface(conn,
        'org.freedesktop.Telepathy.Connection.Interface.Requests')

    CHANNEL_TYPE_CONTACT_LIST = 'org.freedesktop.Telepathy.Channel.Type.ContactList'
    HT_CONTACT_LIST = 3

    # publish
    requestotron.CreateChannel({
        'org.freedesktop.Telepathy.Channel.ChannelType': CHANNEL_TYPE_CONTACT_LIST,
        'org.freedesktop.Telepathy.Channel.TargetHandleType': HT_CONTACT_LIST,
        'org.freedesktop.Telepathy.Channel.TargetID': 'publish'})
    q.expect('dbus-signal', signal='NewChannel')
    # subscribe
    requestotron.CreateChannel({
        'org.freedesktop.Telepathy.Channel.ChannelType': CHANNEL_TYPE_CONTACT_LIST,
        'org.freedesktop.Telepathy.Channel.TargetHandleType': HT_CONTACT_LIST,
        'org.freedesktop.Telepathy.Channel.TargetID': 'subscribe'})
    q.expect('dbus-signal', signal='NewChannel')
    # known
    requestotron.CreateChannel({
        'org.freedesktop.Telepathy.Channel.ChannelType': CHANNEL_TYPE_CONTACT_LIST,
        'org.freedesktop.Telepathy.Channel.TargetHandleType': HT_CONTACT_LIST,
        'org.freedesktop.Telepathy.Channel.TargetID': 'known'})
    q.expect('dbus-signal', signal='NewChannel')
