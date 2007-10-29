#!/usr/bin/env python

from twisted.internet import reactor
from mesh import Mesh, MeshNode, packet_type, WHOIS_REPLY
import sys

NUMNODES = 5
NUMPACKETS = 10
DELAY = 0.1


nodes = []
# We're optimists
success = True

class TestMeshNode(MeshNode):
  nodes = 1

  def __init__ (self, name, mesh):
    MeshNode.__init__(self, name, mesh)

  def node_connected(self):
    MeshNode.node_connected(self)
    print "Connected"

  def newNode (self, data):
    MeshNode.newNode (self, data)
    print "node0 - Added " + data
    self.nodes += 1
    if self.nodes == NUMNODES - 1:
      print "Everybody who could joined"
      for x in xrange(0, NUMPACKETS):
        reactor.callLater(0.1 * x, (lambda y: self.pushInput(str(y) + "\n")), x)

  def leftNode (self, data):
    MeshNode.leftNode (self, data)
    print data.rstrip() + " left"
    reactor.stop()

class FailMeshNode (MeshNode):

  def __init__ (self, name, mesh):
    MeshNode.__init__(self, name, mesh)

  def sendPacket (self, data):
    if packet_type(data) != WHOIS_REPLY:
      MeshNode.sendPacket(self, data)



class TestMesh(Mesh):
  expected = {}
  done = 0

  def gotOutput(self, node, sender, data):
    global success

    if self.expected.get(node) == None:
      self.expected[node] = 0

    if (self.expected.get(node, int(data)) != int(data)):
      print "Got " + data.rstrip() + " instead of " + \
             str(self.expected[node]) + " from "  + node.name

      success = False
      reactor.crash()

    if not sender in node.peers:
      print "Sender " + sender + " not in node peers"
      success = False
      reactor.crash()

    self.expected[node] = int(data) + 1

    if self.expected[node] == 10:
      self.done += 1

    if self.done == NUMNODES - 2:
      for x in self.nodes:
        x.stats()
      self.nodes[-2].disconnect()

m = TestMesh()


n = TestMeshNode("node0", m)
nodes.append(n)
m.addMeshNode(n)

for x in xrange(1, NUMNODES - 1):
  nodes.append(m.addNode("node" + str(x)))

x = NUMNODES - 1
n = FailMeshNode("node" + str(x), m)
nodes.append(n)
m.addMeshNode(n)


# Connect all nodes to all others. 1024 bytes/s bandwidth, 50ms delay and 0%
# packet loss.. (bandwidth and delay aren't implemented just yet)
m.connect_full(1024, 50, 0.30)

def timeout():
  global success
  print "TIMEOUT!"
  success = False
  reactor.crash()

reactor.callLater(60, timeout)

reactor.run()


if not success:
  print "FAILED"
  sys.exit(-1)

print "SUCCESS"
