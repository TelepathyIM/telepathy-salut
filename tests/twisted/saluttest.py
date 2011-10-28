
"""
Infrastructure code for testing Salut
"""

import os
import sys
import time
import re
from subprocess import Popen

import servicetest
from servicetest import call_async, EventPattern, Event, unwrap
from twisted.internet import reactor
import constants as cs
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath
import ns

import dbus
import glib

# keep sync with src/capabilities.c:self_advertised_features
fixed_features = [ns.SI, ns.TUBES, ns.IQ_OOB, ns.X_OOB, ns.TP_FT_METADATA]

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
        'nickname': re.sub('(.*tests/twisted/|\./)', '', sys.argv[0]),
        }

    if params:
        default_params.update(params)

    return servicetest.make_connection(bus, event_func, 'salut',
        'local-xmpp', default_params)

def ensure_avahi_is_running():
    bus = dbus.SystemBus()
    bus_obj = bus.get_object('org.freedesktop.DBus', '/org/freedesktop/DBus')
    if bus_obj.NameHasOwner('org.freedesktop.Avahi',
                            dbus_interface='org.freedesktop.DBus'):
        return

    loop = glib.MainLoop()
    def name_owner_changed_cb(name, old_owner, new_owner):
        loop.quit()

    noc = bus.add_signal_receiver(name_owner_changed_cb,
                                  signal_name='NameOwnerChanged',
                                  dbus_interface='org.freedesktop.DBus',
                                  arg0='org.freedesktop.Avahi')

    # Cannot use D-Bus activation because we have no way to pass to activated
    # clients the address of the system bus and we cannot host the service in
    # this process because we are going to make blocking calls and we would
    # deadlock.
    tests_dir = os.path.dirname(__file__)
    avahimock_path = os.path.join(tests_dir, 'avahimock.py')
    Popen([avahimock_path])

    loop.run()

    noc.remove()

def exec_test_deferred (fun, params, protocol=None, timeout=None,
        make_conn=True):
    colourer = None

    if 'SALUT_TEST_REAL_AVAHI' not in os.environ:
        ensure_avahi_is_running()

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

    def signal_receiver(*args, **kw):
        queue.append(Event('dbus-signal',
                           path=unwrap(kw['path']),
                           signal=kw['member'], args=map(unwrap, args),
                           interface=kw['interface']))

    bus.add_signal_receiver(
        signal_receiver,
        None,       # signal name
        None,       # interface
        None,
        path_keyword='path',
        member_keyword='member',
        interface_keyword='interface',
        byte_arrays=True
        )

    error = None

    try:
        fun(queue, bus, conn)
    except Exception, e:
        import traceback
        traceback.print_exc()
        error = e
        queue.verbose = False

    if colourer:
        sys.stdout = colourer.fh

    if bus.name_has_owner(conn.object.bus_name):
        # Connection hasn't already been disconnected and destroyed
        try:
            if conn.GetStatus() == cs.CONN_STATUS_CONNECTED:
                # Connection is connected, properly disconnect it
                call_async(queue, conn, 'Disconnect')
                queue.expect_many(EventPattern('dbus-signal', signal='StatusChanged',
                                           args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),
                                  EventPattern('dbus-return', method='Disconnect'))
            else:
                # Connection is not connected, call Disconnect() to destroy it
                conn.Disconnect()
        except dbus.DBusException, e:
            pass

        try:
            conn.Disconnect()
            raise AssertionError("Connection didn't disappear; "
                                 "all subsequent tests will probably fail")
        except dbus.DBusException, e:
            pass
        except Exception, e:
            traceback.print_exc()
            error = e

    if error is None:
        reactor.callLater(0, reactor.crash)
    else:
        # please ignore the POSIX behind the curtain
        os._exit(1)

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

def wait_for_contact_in_publish(q, bus, conn, contact_name):
    publish_handle = conn.RequestHandles(cs.HT_LIST, ["publish"])[0]
    publish = conn.RequestChannel(cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.HT_LIST, publish_handle, False)

    handle = 0
    # Wait until the record shows up in publish
    while handle == 0:
        e = q.expect('dbus-signal', signal='MembersChangedDetailed',
                path=publish)
        # Versions of telepathy-glib prior to 0.14.6 incorrectly used the name
        # 'member-ids'.
        try:
            ids = e.args[4]['contact-ids']
        except KeyError:
            ids = e.args[4]['member-ids']

        for h in e.args[0]:
            name = ids[h]
            if name == contact_name:
                handle = h

    return handle

def _elem_add(elem, *children):
    for child in children:
        if isinstance(child, domish.Element):
            elem.addChild(child)
        elif isinstance(child, unicode):
            elem.addContent(child)
        else:
            raise ValueError(
                'invalid child object %r (must be element or unicode)', child)

def elem(a, b=None, attrs={}, **kw):
    r"""
    >>> elem('foo')().toXml()
    u'<foo/>'
    >>> elem('foo', x='1')().toXml()
    u"<foo x='1'/>"
    >>> elem('foo', x='1')(u'hello').toXml()
    u"<foo x='1'>hello</foo>"
    >>> elem('foo', x='1')(u'hello',
    ...         elem('http://foo.org', 'bar', y='2')(u'bye')).toXml()
    u"<foo x='1'>hello<bar xmlns='http://foo.org' y='2'>bye</bar></foo>"
    >>> elem('foo', attrs={'xmlns:bar': 'urn:bar', 'bar:cake': 'yum'})(
    ...   elem('bar:e')(u'i')
    ... ).toXml()
    u"<foo xmlns:bar='urn:bar' bar:cake='yum'><bar:e>i</bar:e></foo>"
    """

    class _elem(domish.Element):
        def __call__(self, *children):
            _elem_add(self, *children)
            return self

    if b is not None:
        elem = _elem((a, b))
    else:
        elem = _elem((None, a))

    # Can't just update kw into attrs, because that *modifies the parameter's
    # default*. Thanks python.
    allattrs = {}
    allattrs.update(kw)
    allattrs.update(attrs)

    # First, let's pull namespaces out
    realattrs = {}
    for k, v in allattrs.iteritems():
        if k.startswith('xmlns:'):
            abbr = k[len('xmlns:'):]
            elem.localPrefixes[abbr] = v
        else:
            realattrs[k] = v

    for k, v in realattrs.iteritems():
        if k == 'from_':
            elem['from'] = v
        else:
            elem[k] = v

    return elem

def elem_iq(server, type, **kw):
    class _iq(IQ):
        def __call__(self, *children):
            _elem_add(self, *children)
            return self

    iq = _iq(server, type)

    for k, v in kw.iteritems():
        if k == 'from_':
            iq['from'] = v
        else:
            iq[k] = v

    return iq

def make_presence(_from, to, type=None, show=None,
        status=None, caps=None, photo=None):
    presence = domish.Element((None, 'presence'))
    presence['from'] = _from
    presence['to'] = to

    if type is not None:
        presence['type'] = type

    if show is not None:
        presence.addElement('show', content=show)

    if status is not None:
        presence.addElement('status', content=status)

    if caps is not None:
        cel = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
        for key,value in caps.items():
            cel[key] = value

    # <x xmlns="vcard-temp:x:update"><photo>4a1...</photo></x>
    if photo is not None:
        x = presence.addElement((ns.VCARD_TEMP_UPDATE, 'x'))
        x.addElement('photo').addContent(photo)

    return presence
