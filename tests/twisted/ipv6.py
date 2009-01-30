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
