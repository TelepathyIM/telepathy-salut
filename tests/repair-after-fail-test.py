#!/usr/bin/env python

# Simulate a node saying bye before any of the other nodes have received all
# it's data
#
# - One sender node + a number of observers is created
# - The sender node waits untill all observers joined it's group
# - It then sends out a number of data packets (0,1,2...)
#     Of which the last two data packets are dropped before hitting the mesh
# - The sender node disconnects (sends bye), causing the observers to notice
#    that they didn't receive all previous packets. And send out repair
#    requests for them, which need to be answered by the leaving node.
#
# This can yield false negatives in two cases:
#   * None of the first two bye packets are ever received by any of the other
#     nodes.
#   * While the leaving node did send out (some) repairs, they never reached
#     by any of the other nodes in time.
#
#   Note that  with 4 observers and 30% change of packet drop:
#    - each packet has a 0.8% change of being missed by every observer.
#    - All observers dropping the first two bye packets has a 0.006% chance

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

class TestMeshNode(MeshNode):
  nodes = 1
  send_data = True

  def __init__ (self, name, mesh):
    MeshNode.__init__(self, name, mesh)

  def node_connected(self):
    MeshNode.node_connected(self)
    print "Connected"

  def push_packet(self, num):
    self.pushInput (str (num) + "\n")
    if (num >= NUMPACKETS - 3):
      self.send_data = False

    if num == NUMPACKETS -1:
      self.disconnect()

  def sendPacket (self, data):
    if (not self.send_data and packet_type(data) == DATA):
      return
    if packet_type(data) == BYE:
      self.send_data = True
    MeshNode.sendPacket (self, data)


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

    MeshNode.leftNode (self, data)
    print data + " left"

    if (data != "node0"):
      print "Wrong node left!"
      success = False
      reactor.stop()
    if (self.mesh.done < observers_done):
      print "Observer done before getting all info"
      success = False
      reactor.stop()

    observers_done += 1

    if (observers_done == NUMOBSERVERS):
      reactor.stop()

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

m = TestMesh()

n = TestMeshNode("node0", m)
nodes.append(n)
m.addMeshNode(n)

for x in xrange(0, NUMOBSERVERS):
  n = ObserverMeshNode("observer" + str(x), m)
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
