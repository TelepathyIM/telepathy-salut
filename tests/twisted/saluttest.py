
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

def exec_test_deferred (fun, params, protocol=None, timeout=None,
        make_conn=True):
    colourer = None

    if sys.stdout.isatty() or 'CHECK_FORCE_COLOR' in os.environ:
        colourer = servicetest.install_colourer()

    bus = dbus.SessionBus()

    queue = servicetest.IteratingEventQueue(timeout)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    if make_conn:
        try:
            conn = make_connection(bus, queue.append, params)
        except Exception, e:
            # This is normally because the connection's still kicking around
            # on the bus from a previous test. Let's bail out unceremoniously.
            print e
            os._exit(1)
    else:
        conn = None

    error = None

    try:
        fun(queue, bus, conn)
    except Exception, e:
        import traceback
        traceback.print_exc()
        error = e
        queue.verbose = False

    try:
        if colourer:
          sys.stdout = colourer.fh

        if error is None:
          reactor.callLater(0, reactor.stop)
        else:
          # please ignore the POSIX behind the curtain
          os._exit(1)

        if conn is not None:
            conn.Disconnect()
    except dbus.DBusException, e:
        pass

    if 'SALUT_TEST_REFDBG' in os.environ:
        # we have to wait that Salut timeouts so the process is properly
        # exited and refdbg can generates its report
        time.sleep(5.5)

def exec_test(fun, params=None, protocol=None, timeout=None,
        make_conn=True):
  reactor.callWhenRunning (exec_test_deferred, fun, params, protocol, timeout,
          make_conn)
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

def wait_for_contact_in_publish(q, bus, conn, contact_name):
    publish_handle = conn.RequestHandles(cs.HT_LIST, ["publish"])[0]
    publish = conn.RequestChannel(cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.HT_LIST, publish_handle, False)

    handle = 0
    # Wait until the record shows up in publish
    while handle == 0:
        e = q.expect('dbus-signal', signal='MembersChangedDetailed',
                path=publish)
        for h in e.args[0]:
            name = e.args[4]['member-ids'][h]
            if name == contact_name:
                handle = h

    return handle
