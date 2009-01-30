import socket
from twisted.internet import tcp

class Ipv6Server(tcp.Server):
    def getHost(self):
        return ('INET6',) + self.socket.getsockname()

    def getPeer(self):
        return ('INET6',) + self.client

    def doRead(self):
      try:
        for i in range(numAccepts):
          # we need this so we can deal with a factory's buildProtocol
          # calling our loseConnection
          if self.disconnecting:
            return
          try:
            skt, addr = self.socket.accept()
          except socket.error, e:
            if e.args[0] in (EWOULDBLOCK, EAGAIN):
              self.numberAccepts = i
              break
            elif e.args[0] == EPERM:
              continue
            raise

          protocol = self.factory.buildProtocol(addr)
          if protocol is None:
            skt.close()
            continue
          s = self.sessionno
          self.sessionno = s+1
          transport = self.transport(skt, protocol, addr, self, s)
          transport = self._preMakeConnection(transport)
          protocol.makeConnection(transport)
        else:
          self.numberAccepts = self.numberAccepts+20
      except:
        log.defer()

class Ipv6Port(tcp.Port):
    addressFamily = socket.AF_INET6

    transport = Ipv6Server

    def getHost(self):
        return ('INET6',) + self.socket.getsockname()

    def getPeer(self):
        return ('INET6',) + self.socket.getpeername()

def listenTCP6(port,factory,backlog=5,interface='::',reactor = None):
  if reactor is None:
    from twisted.internet import reactor
  return reactor.listenWith(Ipv6Port,port,factory,backlog,interface)
