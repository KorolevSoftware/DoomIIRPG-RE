#!/usr/bin/python3
# coding: utf-8

"""
Copyright (c) 2019 DRRP-Team (PROPHESSOR)

This software is released under the MIT License.
https://opensource.org/licenses/MIT
"""

from datetime import datetime

DEBUG = False
GENERATE_OBJ = True
GENERATE_UDMF = False

AXIS_FLAGS = {0: "x", 8: "y", 16: "z"}


def debug(*args):
    if DEBUG:
        print(*args)


class ByteTools:
    def __init__(self, stream):
        self.stream = stream

    def parseUInt(self, x):
        return int.from_bytes(self.stream.read(x), byteorder="little", signed=False)

    def parseUInt8(self):
        return self.parseUInt(1)

    def parseUInt16(self):
        return self.parseUInt(2)

    def parseUInt32(self):
        return self.parseUInt(4)


class Map:
    def __init__(self, stream):
        """stream - ByteTools wrapper"""

        self.parsed = False
        self.parse(stream)

    def parse(self, s):
        self.version = s.parseUInt8()
        self.date = datetime.fromtimestamp(s.parseUInt32())
        self.spawnpos = s.parseUInt16()
        self.spawndir = s.parseUInt8()
        self.mapflags = s.parseUInt8()
        self.secrets = s.parseUInt8()
        self.items = s.parseUInt8()
        self.numnodes = s.parseUInt16()
        self.numleafs = s.parseUInt16()
        self.numlinedefs = s.parseUInt16()
        numnormals = s.parseUInt16()
        numpolys = s.parseUInt16()
        numverts = s.parseUInt16()
        self.numspritesnormal = s.parseUInt16()
        self.numspritesz = s.parseUInt16()
        self.numtileevents = s.parseUInt16()
        self.bytecodesize = s.parseUInt16()
        self.numcameras = s.parseUInt8()
        self.numcamkeys = s.parseUInt16()
        self.numtweenss = [s.parseUInt16() for _ in range(6)]

        if not self._assertMarker(s, 0xDEADBEEF, "file header"):
            return

        numtexids = s.parseUInt16()
        print("media_count", numtexids)
        self.texids = [s.parseUInt16() for _ in range(numtexids)]

        if not self._assertMarker(s, 0xDEADBEEF, "textures section"):
            return

        self.normals = [
            {"x": s.parseUInt16(), "y": s.parseUInt16(), "z": s.parseUInt16()}
            for _ in range(numnormals)
        ]

        if not self._assertMarker(s, 0xCAFEBABE, "normals section"):
            return

        s.stream.seek(self.numnodes * 2, 1)

        if not self._assertMarker(s, 0xCAFEBABE, "nodes section (1)"):
            return

        s.stream.seek(self.numnodes, 1)

        if not self._assertMarker(s, 0xCAFEBABE, "nodes section (2)"):
            return

        s.stream.seek(self.numnodes * 2 * 2, 1)

        if not self._assertMarker(s, 0xCAFEBABE, "nodes section (3)"):
            return

        s.stream.seek(self.numnodes * 2 * 2, 1)

        if not self._assertMarker(s, 0xCAFEBABE, "nodes section (4)"):
            return

        s.stream.seek(self.numleafs * 2 * 2, 1)

        if not self._assertMarker(s, 0xCAFEBABE, "leafs section"):
            return

        self.polygons = [{"texture": s.parseUInt8()} for _ in range(numpolys)]

        for polygon in self.polygons:
            flags = polygon["flags"] = s.parseUInt8()

            if not (flags & 7):  # If it's a rect (vertices == 2)
                axis = AXIS_FLAGS[flags & 24]

        self.vertices = [{} for _ in range(numverts)]

        for value in ("x", "y", "z", "u", "v"):
            for vertice in self.vertices:
                vertice[value] = s.parseUInt8()

        if not self._assertMarker(s, 0xCAFEBABE, "polygons section"):
            return

        debug("Map was parsed successfully!")

        self.parsed = True

    def _assertMarker(self, stream, marker, section="file section"):
        m = stream.parseUInt32()

        if m != marker:
            print(
                "[ERROR]: Unknown marker %x at the end of %s! Is it a Doom II RPG J2ME map? (Required: %x)"
                % (m, section, marker)
            )
            return False

        return True

    def info(self):
        if not self.parsed:
            return

        print("Version: %d" % self.version)
        print("Created at: %s" % self.date)
        print("Spawnpos: %d" % self.spawnpos)
        print("Spawndir: %d" % self.spawndir)
        print("Flags: %d" % self.mapflags)
        print("Secrets: %d" % self.secrets)
        print("Items: %d" % self.items)
        print("Nodes: %d" % self.numnodes)
        print("Leafs: %d" % self.numleafs)
        print("Linedefs: %d" % self.numlinedefs)
        print("Normals: %d" % len(self.normals))
        print("Polygons: %d" % len(self.polygons))
        print("Vertices: %d" % len(self.vertices))
        print("Sprite normals: %d" % self.numspritesnormal)
        print("Sprites Z: %d" % self.numspritesz)
        print("Tile Events: %d" % self.numtileevents)
        print("Bytecode Size: %d" % self.bytecodesize)
        print("Cameras: %d" % self.numcameras)
        print("Camera keys: %d" % self.numcamkeys)
        print("Tweenss:", self.numtweenss)
        print("Textures: ", len(self.texids))

    def toObj(self):
        if not self.parsed:
            return ""

        obj = []

        vertid = 0
        outvertid = 0

        for polygon in self.polygons:
            polyverts = (polygon["flags"] & 7) + 2

            if polyverts != 2:  # Common polygon
                for _ in range(polyverts):
                    debug("vertid", vertid)
                    vert = self.vertices[vertid]

                    obj.append("v %d %d %d" % (vert["x"], vert["y"], vert["z"]))

                    vertid += 1
                    outvertid += 1
            else:  # Rectangular polygon
                pass
                new_polygon = [
                    self.vertices[vertid],
                    {**self.vertices[vertid]},
                    self.vertices[vertid + 1],
                    {**self.vertices[vertid + 1]},
                ]

                # Rotate the polygon dependent of axis
                axis = AXIS_FLAGS[polygon["flags"] & 24]

                new_polygon[1][axis] = new_polygon[2][axis]
                new_polygon[3][axis] = new_polygon[0][axis]

                polyverts = 4

                for vertex in new_polygon:
                    obj.append("v %d %d %d" % (vertex["x"], vertex["y"], vertex["z"]))
                    outvertid += 1

                vertid += 2

            # Generate faces
            face = ["f"]

            for j in range(outvertid - polyverts, outvertid):
                face.append(str(j + 1))
            obj.append(" ".join(face))

        return "\n".join(obj)

    def toUdmf(self):
        if not self.parsed:
            return ""

        print("WARNING! UDMF export is not fully implemented yet!")

        obj = []

        v = []

        vertid = 0
        outvertid = 0

        for polygon in self.polygons:
            polyverts = (polygon["flags"] & 7) + 2

            if polyverts != 2:  # Common polygon
                for _ in range(polyverts):
                    debug("vertid", vertid)
                    vert = self.vertices[vertid]
                    #                   obj.append("vertex {")
                    #                   obj.append("  x=%d;" % vert['x'])
                    #                   obj.append("  y=%d;" % vert['y'])
                    #                   obj.append("}")

                    vertid += 1
                    outvertid += 1
            else:  # Rectangular polygon
                # FIXME:
                def depack(vertex):
                    return (vertex["x"], vertex["y"])

                lines = [
                    (depack(self.vertices[vertid]), depack(self.vertices[vertid + 1]))
                ]

                if AXIS_FLAGS[polygon["flags"] & 24] != "z":
                    lines = []
                #                   lines = [
                #                       (
                #                           depack(self.vertices[vertid]),
                #                           (self.vertices[vertid + 1]['x'], self.vertices[vertid]['y'])
                #                       ),
                #                       (
                #                           (self.vertices[vertid + 1]['x'], self.vertices[vertid]['y']),
                #                           depack(self.vertices[vertid + 1]),
                #                       ),
                #                       (
                #                           depack(self.vertices[vertid + 1]),
                #                           (self.vertices[vertid]['x'], self.vertices[vertid + 1]['y'])
                #                       ),
                #                       (
                #                           (self.vertices[vertid]['x'], self.vertices[vertid + 1]['y']),
                #                           depack(self.vertices[vertid])
                #                       )
                #                   ]

                for line in lines:
                    if not line[0] in v:
                        v.append(line[0])
                    v1 = v.index(line[0])

                    if not line[1] in v:
                        v.append(line[1])
                    v2 = v.index(line[1])

                    obj.append("linedef {")
                    obj.append("  v1=%d;" % v1)
                    obj.append("  v2=%d;" % v2)
                    obj.append("  sidefront=0;")
                    obj.append("}")

                outvertid += 4

                vertid += 2

            # Generate faces
        #           for j in range(outvertid - polyverts, outvertid, 2):
        #               obj.append("linedef {")
        #               obj.append("  v1=%d;" % j)
        #               obj.append("  v2=%d;" % (j + 1))
        #               obj.append("  sidefront=0;")
        #               obj.append("}")

        for vv in v:
            obj.append("vertex {")
            obj.append("  x=%d;" % (vv[0] * 8))
            obj.append("  y=%d;" % (vv[1] * 8))
            obj.append("}")

        obj.append("sidedef {")
        obj.append("  sector=0;")
        obj.append("}")

        obj.append("sector {")
        obj.append('  texturefloor="ROCK3";')
        obj.append('  textureceiling="F_SKY1";')
        obj.append("  heightfloor=0;")
        obj.append("  heightceiling=128;")
        obj.append("  lightlevel=160;")
        obj.append("}")

        return "\n".join(obj)


def main(argv):
    with open(argv[1], "rb") as _in:
        _map = Map(ByteTools(_in))

    _map.info()

    if GENERATE_OBJ:
        with open(argv[1][:-4] + ".obj", "w") as _out:
            _out.write(_map.toObj())
            print("OBJ generated successfully!")
    if GENERATE_UDMF:
        with open(argv[1][:-4] + ".udmf", "w") as _out:
            _out.write(_map.toUdmf())
            print("UDMF generated successfully!")


if __name__ == "__main__":
    import sys

    main(sys.argv)
