import socket
from twisted.internet import tcp

class Ipv6Server(tcp.Server):
    def getHost(self):
        return IPv6Address('TCP', *(self.socket.getsockname()))

    def getPeer(self):
        return IPv6Address('TCP', *(self.client))

class Ipv6Port(tcp.Port):
    addressFamily = socket.AF_INET6

    transport = Ipv6Server

    def _buildAddr(self, (host, port, flowinfo, scopeid)):
        """
        Build and return an IPv6Address from the passed sockaddr-in6 tuple.
        """
        return IPv6Address('TCP', host, port, flowinfo, scopeid)

    def getHost(self):
        return IPv6Address('TCP', *(self.socket.getsockname()))

def listenTCP6(port,factory,backlog=5,interface='::',reactor = None):
  if reactor is None:
    from twisted.internet import reactor
  return reactor.listenWith(Ipv6Port,port,factory,backlog,interface)


# stolen from http://twistedmatrix.com/trac/attachment/ticket/3014/ipv6.2.patch
from zope.interface import implements
from twisted.internet.interfaces import IAddress
from twisted.internet import base, address, error
from twisted.internet.tcp import Client
from twisted.python.util import unsignedID
import types

class IPv6Address(object):
    """
    Object representing an IPv6 socket endpoint.

    @ivar type: A string describing the type of transport, either 'TCP' or 'UDP'.
    @ivar host: A string containing the coloned-oct IP address.
    @ivar port: An integer representing the port number.
    @ivar flowinfo: An integer representing the sockaddr-in6 flowinfo
    @ivar scopeid: An integer representing the sockaddr-in6 scopeid
    """

    implements(IAddress)

    def __init__(self, type, host, port, flowinfo=0, scopeid=0):
        if type not in ('TCP', 'UDP'):
            raise ValueError, "illegal transport type"
        self.type = type
        self.host = host
        self.port = port
        self.flowinfo = flowinfo
        self.scopeid = scopeid

    def __eq__(self, other):
        if isinstance(other, tuple):
            return tuple(self) == other
        elif isinstance(other, IPv6Address):
            a = (self.type, self.host, self.port, self.flowinfo, self.scopeid)
            b = (other.type, other.host, other.port, other.flowinfo, other.scopeid)
            return a == b
        return False

    def __str__(self):
        return 'IPv6Address(%s, %r, %d, %s, %s, %s)' % (self.type, self.host,
          self.port, self.flowinfo, self.scopeid)

class Client6(Client):
    """
    A TCP6 client.
    """
    addressFamily = socket.AF_INET6

    def __init__(self, host, port, bindAddress, connector, reactor=None,
                 flowinfo=0, scopeid=0):
        Client.__init__(self, host, port, bindAddress, connector, reactor)
        self.addr = (host, port, flowinfo, scopeid)

    def resolveAddress(self):
        """
        Lookup the IPv6 address for self.addr[0] if necessary, then set
        self.realAddress to that IPv6 address.
        """
        if isIPv6Address(self.addr[0]):
            self._setRealAddress(self.addr[0])
        else:
            d = self.reactor.resolve(self.addr[0])
            d.addCallbacks(self._setRealAddress, self.failIfNotConnected)

    def _setRealAddress(self, address):
        """
        Set self.realAddress[0] to address.  Set the remaining parts of 
        self.realAddress to the corresponding parts of self.addr.
        """
        self.realAddress = (address, self.addr[1], self.addr[2], self.addr[3])
        self.doConnect()

    def getHost(self):
        """
        Returns an IPv6Address.

        This indicates the address from which I am connecting.
        """
        return address.IPv6Address('TCP', *(self.socket.getsockname()))

    def getPeer(self):
        """
        Returns an IPv6Address.

        This indicates the address that I am connected to.
        """
        return IPv6Address('TCP', *(self.addr))

    def __repr__(self):
        s = '<%s to %s at %x>' % (self.__class__, self.addr, unsignedID(self))
        return s

class Connector6(base.BaseConnector):
    """
    IPv6 implementation of connector

    @ivar flowinfo An integer representing the sockaddr-in6 flowinfo
    @ivar scopeid An integer representing the sockaddr-in6 scopeid
    """

    def __init__(self, host, port, factory, timeout, bindAddress,
                 reactor=None, flowinfo=0, scopeid=0):
        self.host = host
        if isinstance(port, types.StringTypes):
            try:
                port = socket.getservbyname(port, 'tcp')
            except socket.error, e:
                raise error.ServiceNameUnknownError(string="%s (%r)" % (e, port))
        self.port = port
        self.bindAddress = bindAddress
        self.flowinfo = flowinfo
        self.scopeid = scopeid
        base.BaseConnector.__init__(self, factory, timeout, reactor)

    def _makeTransport(self):
        """
        Build and return a TCP6 client for the connector's transport.
        """
        return Client6(self.host, self.port, self.bindAddress, self,
                      self.reactor, self.flowinfo, self.scopeid)

    def getDestination(self):
        """
        @see twisted.internet.interfaces.IConnector.getDestination
        """
        return address.IPv6Address('TCP', self.host, self.port, self.flowinfo,
                                   self.scopeid)

def connectTCP6(reactor, host, port, factory, timeout=30, bindAddress=None,
        flowinfo=0, scopeid=0):
        """
        @see: twisted.internet.interfaces.IReactorTCP.connectTCP6
        """
        c = Connector6(host, port, factory, timeout, bindAddress, reactor,
                           flowinfo, scopeid)
        c.connect()
        return c

def isIPv6Address(ip):
    """
    Return True iff ip is a valid bare IPv6 address.

    Return False for 'enhanced' IPv6 addresses like '::1%lo' and '::1/128'
    """
    try:
        socket.inet_pton(socket.AF_INET6, ip)
    except (ValueError, socket.error):
        return False
    return True
