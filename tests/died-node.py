#!/usr/bin/env python

# Both have a very small chance of occuring

from twisted.internet import reactor
from mesh import Mesh, MeshNode, packet_type, BYE, DATA
import sys

NUMNODES = 2
DELAY = 0.1

m = Mesh()
success = False

class TestMeshNode(MeshNode):

  def newNode (self, data):
    MeshNode.newNode (self, data)
    print data + " joined"
    if (data == failnode.name):
      m.removeMeshNode(failnode)
      n = MeshNode("joinnode", m)
      m.addMeshNode(n)
      m.connect_duplex (self, n, 1024, 50, 0.30)
    if (data == "joinnode"):
      global success
      success = True
      reactor.crash()


  def lostNode (self, data):
    MeshNode.lostNode (self, data)
    print data + " lost"

n = TestMeshNode("node", m)
m.addMeshNode(n)

failnode = MeshNode("failnode", m)
m.addMeshNode(failnode)

# Connect all nodes to all others. 1024 bytes/s bandwidth, 50ms delay and 0%
# packet loss.. (bandwidth and delay aren't implemented just yet)
m.connect_full(1024, 50, 0.30)

reactor.run()

if not success:
  print "FAILED"
  exit(1)

print "SUCCESS"
exit(0)

