#!/usr/bin/env python

# Both have a very small chance of occuring

from twisted.internet import reactor
from mesh import Mesh, MeshNode, packet_type, BYE, DATA
import sys

NUMNODES = 2
DELAY = 0.1

m = Mesh()

class TestMeshNode(MeshNode):

  def newNode (self, data):
    MeshNode.newNode (self, data)
    print data + " joined"
    self.fail(data)

  def leftNode (self, data):
    MeshNode.leftNode (self, data)
    print data + " left"

n = TestMeshNode("node", m)
m.addMeshNode(n)

failnode = MeshNode("failnode", m)
m.addMeshNode(failnode)

# Connect all nodes to all others. 1024 bytes/s bandwidth, 50ms delay and 0%
# packet loss.. (bandwidth and delay aren't implemented just yet)
m.connect_full(1024, 50, 0.30)

reactor.run()
