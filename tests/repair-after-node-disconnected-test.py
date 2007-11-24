#!/usr/bin/env python

# Simulate a node leaving and one of the remaining nodes not having retrieved
# all info untill after the failure process is running.. Thus needing to get
# repairs from peers that already have failed the left node
#
# - One sender node, a number of observers and a retriever are created
# - The sender node waits untill all observers joined it's group
# - It then sends out a number of data packets (0,1,2...)
#     Of which the last packets are dropped before being received by the
#     receiver.
# - The sender node disconnects (sends bye). Triggering the other nodes to
#   start the failure process, depend on all packets the sender has sent. At
#   the same time the retriever is asked to fail the sender. Which means
#   everyone will complete the failure process except for the retriever, who
#   misses some of the senders packets.  Only after the sender node is fully
#   gone, the retrivier is allowed to retrieve it's data packet.. Thus it needs
#   to be able to retrieve them from its remaining peers
# - After successfull finishing an extra packet is send by the retriever, which
#   has the side-effect of acking all packets of node0. The debug output should
#   reveal that node0 is disposed by everyone shortly afterwards
#
# This can yield false negatives in two cases:
#   * The bye packets aren't received by any of the observers/retriever.
#   * The complete set of packets send out isn't ``in the network'' after the
#     sender leaves
#
# Both have a very small chance of occuring

from twisted.internet import reactor
from mesh import Mesh, MeshNode, packet_type, BYE, DATA
import sys

NUMOBSERVERS = 4
NUMPACKETS = 10
DELAY = 0.1

nodes = []
# We're optimists
success = True

observers_done = 0
retriever_receiving = True

class TestMeshNode(MeshNode):
  nodes = 1

  def __init__ (self, name, mesh):
    MeshNode.__init__(self, name, mesh)

  def node_connected(self):
    MeshNode.node_connected(self)
    print "Connected"

  def node_disconnected(self):
    global retriever

    # Let the retriever receive packets again and let it fail the sender
    retriever.fail(self.name)
    MeshNode.node_disconnected(self)

  def push_packet(self, num):
    global retriever_receiving
    self.pushInput (str (num) + "\n")

    if num >= NUMPACKETS - 3:
      retriever_receiving = False

    if num == NUMPACKETS - 1:
      self.disconnect()

  def newNode (self, data):
    MeshNode.newNode (self, data)
    print "node0 - Added " + data
    self.nodes += 1
    if self.nodes == NUMOBSERVERS + 1:
      print "Everybody who could joined"
      for x in xrange(0, NUMPACKETS):
        reactor.callLater(0.1 * x, (lambda y: self.push_packet(y)), x)

class ObserverMeshNode(MeshNode):
  def __init__ (self, name, mesh):
    MeshNode.__init__(self, name, mesh)

  def leftNode (self, data):
    global observers_done
    global retriever_receiving
    global success

    MeshNode.leftNode (self, data)
    print self.name + " => " + data + " left"

    if (data != "node0"):
      print "Wrong node left!"
      success = False
      reactor.crash()
      return

    if (self.mesh.done < observers_done):
      print "Observer done before getting all info"
      success = False
      reactor.crash()
      return

    observers_done += 1

    if (observers_done == NUMOBSERVERS -1):
      retriever_receiving = True

    if (observers_done == NUMOBSERVERS):
      reactor.crash()
      retriever.pushInput("blaat\n");

class RetrieverMeshNode(ObserverMeshNode):
  def __init__ (self, name, mesh):
    ObserverMeshNode.__init__(self, name, mesh)

  def recvPacket (self, data):
    if retriever_receiving:
      ObserverMeshNode.recvPacket(self, data)

class TestMesh(Mesh):
  expected = {}
  done = 0

  def gotOutput(self, node, sender, data):
    global success

    if sender == "retriever":
      return

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

m = TestMesh()

n = TestMeshNode("node0", m)
nodes.append(n)
m.addMeshNode(n)

for x in xrange(0, NUMOBSERVERS - 1):
  n = ObserverMeshNode("observer" + str(x), m)
  nodes.append(n)
  m.addMeshNode(n)

retriever = RetrieverMeshNode("retriever", m)
nodes.append(retriever)
m.addMeshNode(retriever)

# Connect all nodes to all others. 1024 bytes/s bandwidth, 50ms delay and 0%
# packet loss.. (bandwidth and delay aren't implemented just yet)
m.connect_full(1024, 50, 0.30)

def timeout():
  global success
  print "TIMEOUT!"
  success = False
  reactor.crash()

id = reactor.callLater(60, timeout)
reactor.run()

id.cancel()

if not success:
  print "FAILED"
  sys.exit(-1)

print "SUCCES!!.. Waiting 30 before exiting"

reactor.callLater(30, reactor.stop)
reactor.run()
