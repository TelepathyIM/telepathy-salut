
"""
Infrastructure code testing CM by pretending to be a Jabber server.
"""

import base64
import os
import sha
import sys

import servicetest
import twisted
from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ
from twisted.words.protocols.jabber import xmlstream
from twisted.internet import reactor

import dbus

NS_XMPP_SASL = 'urn:ietf:params:xml:ns:xmpp-sasl'
NS_XMPP_BIND = 'urn:ietf:params:xml:ns:xmpp-bind'

def make_result_iq(stream, iq):
    result = IQ(stream, "result")
    result["id"] = iq["id"]
    query = iq.firstChildElement()

    if query:
        result.addElement((query.uri, query.name))

    return result

def acknowledge_iq(stream, iq):
    stream.send(make_result_iq(stream, iq))

def sync_stream(q, stream):
    """Used to ensure that Gabble has processed all stanzas sent to it."""

    iq = IQ(stream, "get")
    iq.addElement(('http://jabber.org/protocol/disco#info', 'query'))
    stream.send(iq)
    q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info')

class JabberAuthenticator(xmlstream.Authenticator):
    "Trivial XML stream authenticator that accepts one username/digest pair."

    def __init__(self, username, password):
        self.username = username
        self.password = password
        xmlstream.Authenticator.__init__(self)

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        self.xmlstream.sendHeader()
        self.xmlstream.addOnetimeObserver(
            "/iq/query[@xmlns='jabber:iq:auth']", self.initialIq)

    def initialIq(self, iq):
        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        query = result.addElement('query')
        query["xmlns"] = "jabber:iq:auth"
        query.addElement('username', content='test')
        query.addElement('password')
        query.addElement('digest')
        query.addElement('resource')
        self.xmlstream.addOnetimeObserver('/iq/query/username', self.secondIq)
        self.xmlstream.send(result)

    def secondIq(self, iq):
        username = xpath.queryForNodes('/iq/query/username', iq)
        assert map(str, username) == [self.username]

        digest = xpath.queryForNodes('/iq/query/digest', iq)
        expect = sha.sha(self.xmlstream.sid + self.password).hexdigest()
        assert map(str, digest) == [expect]

        resource = xpath.queryForNodes('/iq/query/resource', iq)
        assert map(str, resource) == ['Resource']

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        self.xmlstream.send(result)
        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)


class XmppAuthenticator(xmlstream.Authenticator):
    def __init__(self, username, password):
        xmlstream.Authenticator.__init__(self)
        self.username = username
        self.password = password
        self.authenticated = False

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        self.xmlstream.sendHeader()

        if self.authenticated:
            # Initiator authenticated itself, and has started a new stream.

            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            bind = features.addElement((NS_XMPP_BIND, 'bind'))
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver(
                "/iq/bind[@xmlns='%s']" % NS_XMPP_BIND, self.bindIq)
        else:
            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            mechanisms = features.addElement((NS_XMPP_SASL, 'mechanisms'))
            mechanism = mechanisms.addElement('mechanism', content='PLAIN')
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver("/auth", self.auth)

    def auth(self, auth):
        assert (base64.b64decode(str(auth)) ==
            '\x00%s\x00%s' % (self.username, self.password))

        success = domish.Element((NS_XMPP_SASL, 'success'))
        self.xmlstream.send(success)
        self.xmlstream.reset()
        self.authenticated = True

    def bindIq(self, iq):
        assert xpath.queryForString('/iq/bind/resource', iq) == 'Resource'

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        bind = result.addElement((NS_XMPP_BIND, 'bind'))
        jid = bind.addElement('jid', content='test@localhost/Resource')
        self.xmlstream.send(result)

        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)

def make_stream_event(type, stanza):
    event = servicetest.Event(type, stanza=stanza)
    event.to = stanza.getAttribute("to")
    return event

def make_iq_event(iq):
    event = make_stream_event('stream-iq', iq)
    event.iq_type = iq.getAttribute("type")
    query = iq.firstChildElement()

    if query:
        event.query = query
        event.query_ns = query.uri
        event.query_name = query.name

        if query.getAttribute("node"):
            event.query_node = query.getAttribute("node")

    return event

def make_presence_event(stanza):
    event = make_stream_event('stream-presence', stanza)
    event.presence_type = stanza.getAttribute('type')
    return event

def make_message_event(stanza):
    event = make_stream_event('stream-message', stanza)
    event.message_type = stanza.getAttribute('type')
    return event

class BaseXmlStream(xmlstream.XmlStream):
    initiating = False
    namespace = 'jabber:client'

    def __init__(self, event_func, authenticator):
        xmlstream.XmlStream.__init__(self, authenticator)
        self.event_func = event_func
        self.addObserver('//iq', lambda x: event_func(
            make_iq_event(x)))
        self.addObserver('//message', lambda x: event_func(
            make_message_event(x)))
        self.addObserver('//presence', lambda x: event_func(
            make_presence_event(x)))
        self.addObserver('//event/stream/authd', self._cb_authd)

    def _cb_authd(self, _):
        # called when stream is authenticated
        self.addObserver(
            "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
            self._cb_disco_iq)
        self.event_func(servicetest.Event('stream-authenticated'))

    def _cb_disco_iq(self, iq):
        if iq.getAttribute('to') == 'localhost':
            # add PEP support
            nodes = xpath.queryForNodes(
                "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
                iq)
            query = nodes[0]
            identity = query.addElement('identity')
            identity['category'] = 'pubsub'
            identity['type'] = 'pep'

            iq['type'] = 'result'
            self.send(iq)

class JabberXmlStream(BaseXmlStream):
    version = (0, 9)

class XmppXmlStream(BaseXmlStream):
    version = (1, 0)

def make_stream(event_func, authenticator=None, protocol=None, port=4242):
    # set up Jabber server

    if authenticator is None:
        authenticator = JabberAuthenticator('test', 'pass')

    if protocol is None:
        protocol = JabberXmlStream

    stream = protocol(event_func, authenticator)
    factory = twisted.internet.protocol.Factory()
    factory.protocol = lambda *args: stream
    port = reactor.listenTCP(port, factory)
    return (stream, port)
