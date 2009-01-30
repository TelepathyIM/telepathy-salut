
"""
Infrastructure code for testing. Implements incoming and outgoing xml/xmpp
streams
"""

import servicetest
from servicetest import Event, EventPattern
import twisted
from twisted.words.xish import domish, xpath, xmlstream
from twisted.internet.protocol import Factory, ClientFactory
from twisted.internet import reactor

from ipv6 import listenTCP6

NS_STREAMS = 'http://etherx.jabber.org/streams'

def make_stream_event(type, stanza):
    event = servicetest.Event(type, stanza=stanza)
    if stanza.hasAttribute("to"):
        event.name = stanza.getAttribute("to")
    if stanza.hasAttribute("from"):
        event.remote_name = stanza.getAttribute("from")
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
    prefixes = { NS_STREAMS: 'stream' }
    version = "1.0"

    def __init__(self, event_function, name = None, remote_name = None):
        xmlstream.XmlStream.__init__(self)

        self.name = name
        self.remote_name = remote_name
        self.event_func = event_function

        self.event_function = event_function
        self.addObserver(xmlstream.STREAM_START_EVENT,
            lambda *args: self.event(Event('stream-opened')))
        self.addObserver('//features', lambda x: self.event(
            make_stream_event('stream-features', x)))
        self.addObserver('//iq', lambda x: self.event(
            make_iq_event(x)))
        self.addObserver('//message', lambda x: self.event(
            make_message_event(x)))
        self.addObserver('//presence', lambda x: self.event(
            make_presence_event(x)))

    def send_header(self):
        root = domish.Element((NS_STREAMS, 'stream'))
        if self.name is not None:
            root['from'] = self.name
        if self.remote_name is not None:
            root['to'] = self.remote_name
        root['version'] = self.version
        self.send(root.toXml(closeElement = 0, prefixes=self.prefixes))

    def event(self, e):
        e.connection = self
        self.event_function(e)

    def send(self, obj):
        if domish.IElement.providedBy(obj):
            if self.name != None:
                obj["from"] = self.name
            if self.remote_name != None:
                obj["to"] = self.remote_name
            obj = obj.toXml(prefixes=self.prefixes)

        xmlstream.XmlStream.send(self, obj)


class IncomingXmppStream(BaseXmlStream):
    def __init__(self, event_func, name):
        BaseXmlStream.__init__(self, event_func, name, None)

    def onDocumentStart(self, rootElement):
        # Use the fact that it's always salut that connects, so it sends a
        # proper opening
        assert rootElement.name == "stream"
        assert rootElement.uri == NS_STREAMS

        assert rootElement.hasAttribute("from")
        assert rootElement.hasAttribute("to")
        if self.name is not None:
            assert rootElement["to"] == self.name, self.name

        assert rootElement.hasAttribute("version")
        assert rootElement["version"] == "1.0"

        self.remote_name = rootElement["from"]
        self.send_header()
        self.send_features()
        BaseXmlStream.onDocumentStart(self, rootElement)

    def send_features(self):
        features = domish.Element((NS_STREAMS, 'features'))
        self.send(features)

class IncomingXmppFactory(Factory):
    def buildProtocol(self, addr):
        p = self.protocol()
        p.factory = self
        e = Event('incoming-connection', listener = self)
        p.event(e)
        return p

def setup_stream_listener(queue, name, port = 0, protocol = None):
    if protocol == None:
        protocol = IncomingXmppStream

    factory = IncomingXmppFactory()
    factory.protocol = lambda *args: protocol(queue.append, name)
    port = reactor.listenTCP(port, factory)

    return (factory, port.getHost().port)

def setup_stream_listener6(queue, name, port = 0, protocol = None):
    if protocol == None:
        protocol = IncomingXmppStream

    factory = IncomingXmppFactory()
    factory.protocol = lambda *args: protocol(queue.append, name)
    port = listenTCP6(port, factory)

    return (factory, port.getHost().port)

class OutgoingXmppStream(BaseXmlStream):
    def __init__(self, event_function, name, remote_name):
        BaseXmlStream.__init__(self, event_function, name, remote_name)
        self.addObserver(xmlstream.STREAM_CONNECTED_EVENT, self.connected)

    def connected (self, stream):
        e = Event('connection-result', succeeded = True)
        self.event(e)

        self.send_header()

class OutgoingXmppiChatStream(OutgoingXmppStream):
    def __init__(self, event_function, name, remote_name):
        # set name and remote_name as None as iChat doesn't send 'to' and
        # 'from' attributes.
        OutgoingXmppStream.__init__(self, event_function, None, None)

class IncomingXmppiChatStream(IncomingXmppStream):
    def __init__(self, event_func, name):
        # set name to None as iChat doesn't send 'from' attribute.
        IncomingXmppStream.__init__(self, event_func, None)

class OutgoingXmppFactory(ClientFactory):
    def __init__(self, event_function):
        self.event_func = event_function

    def clientConnectionFailed(self, connector, reason):
        ClientFactory.clientConnectionFailed(self, connector, reason)
        e = Event('connection-result', succeeded = False, reason = reason)
        self.event_func(e)

def connect_to_stream(queue, name, remote_name, host, port, protocol = None):
    if protocol == None:
        protocol = OutgoingXmppStream

    p = protocol(queue.append, name, remote_name)

    factory = OutgoingXmppFactory(queue.append)
    factory.protocol = lambda *args: p
    reactor.connectTCP(host, port, factory)

    return p

if __name__ == '__main__':
    def run_test():
        q = servicetest.IteratingEventQueue()
        # Set verboseness if needed for debugging
        #q.verbose = True

        (listener, port) = setup_stream_listener(q, "incoming")
        outbound = connect_to_stream(q, "outgoing",
            "incoming", "localhost", port)

        inbound = q.expect('incoming-connection',
            listener = listener).connection

        # inbound stream is opened first, then outbounds stream is opened and
        # receive features
        q.expect('stream-opened', connection = inbound)
        q.expect('stream-opened', connection = outbound)
        q.expect('stream-features', connection = outbound)


        message = domish.Element(('','message'))
        message.addElement('body', content="test123")
        outbound.send(message)

        e = q.expect('stream-message', connection=inbound)

        # twisting twisted
        reactor.stop()

    reactor.callLater(0.1, run_test)
    reactor.run()
