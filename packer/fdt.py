"""Minimal pure-Python Flattened Device Tree reader/editor.

Only what the boot builder needs: find/create a node, set/delete properties,
and re-serialize with slack so properties can grow.  Keeping this in Python
lets the Windows build work without dtc/fdtput.
"""

import struct

MAGIC = b"\xd0\x0d\xfe\xed"
BEGIN_NODE, END_NODE, PROP, NOP, END = 1, 2, 3, 4, 9
HEADER = struct.Struct(">9I")


class FdtError(Exception):
    pass


class Node:
    def __init__(self, name, parent=None):
        self.name = name
        self.parent = parent
        self.props = []          # list of [name, bytes]
        self.children = []

    def child(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def get(self, name):
        for n, v in self.props:
            if n == name:
                return v
        return None

    def set(self, name, value):
        for item in self.props:
            if item[0] == name:
                item[1] = value
                return
        self.props.append([name, value])

    def delete(self, name):
        self.props = [p for p in self.props if p[0] != name]


class Fdt:
    def __init__(self, root, rsvmap, boot_cpuid=0):
        self.root = root
        self.rsvmap = rsvmap
        self.boot_cpuid = boot_cpuid

    @classmethod
    def parse(cls, blob):
        if len(blob) < 40 or blob[:4] != MAGIC:
            raise FdtError("not a DTB")
        (total, off_struct, off_strings, off_rsv, version,
         last_comp, boot_cpuid, size_strings, size_struct) = HEADER.unpack_from(blob, 4)
        if total > len(blob):
            raise FdtError("truncated DTB")
        if off_struct + size_struct > total or off_strings + size_strings > total:
            raise FdtError("bad DTB block bounds")

        # reserved memory map: pairs of u64 until a zero pair
        rsv = bytearray()
        pos = off_rsv
        while pos + 16 <= total:
            pair = blob[pos:pos + 16]
            rsv += pair
            pos += 16
            if pair == b"\0" * 16:
                break

        strings = blob[off_strings:off_strings + size_strings]
        pos = off_struct
        end = off_struct + size_struct
        stack = []
        root = None
        while pos < end:
            (tok,) = struct.unpack_from(">I", blob, pos)
            pos += 4
            if tok == BEGIN_NODE:
                nul = blob.index(b"\0", pos)
                name = blob[pos:nul].decode("utf-8", "replace")
                pos = (nul + 1 + 3) & ~3
                node = Node(name, stack[-1] if stack else None)
                if stack:
                    stack[-1].children.append(node)
                else:
                    if root is not None:
                        raise FdtError("multiple root nodes")
                    root = node
                stack.append(node)
            elif tok == END_NODE:
                if not stack:
                    raise FdtError("unbalanced END_NODE")
                stack.pop()
            elif tok == PROP:
                if not stack:
                    raise FdtError("property outside node")
                (ln, nameoff) = struct.unpack_from(">II", blob, pos)
                pos += 8
                data = blob[pos:pos + ln]
                pos = (pos + ln + 3) & ~3
                if nameoff >= len(strings):
                    raise FdtError("bad property name offset")
                nul = strings.index(b"\0", nameoff)
                name = strings[nameoff:nul].decode("utf-8", "replace")
                stack[-1].props.append([name, bytes(data)])
            elif tok == NOP:
                pass
            elif tok == END:
                break
            else:
                raise FdtError("bad token 0x%x" % tok)
        if root is None or stack:
            raise FdtError("malformed DTB tree")
        return cls(root, bytes(rsv), boot_cpuid)

    def node(self, path, create=False):
        if not path.startswith("/"):
            raise FdtError("path must be absolute")
        if path == "/":
            return self.root
        cur = self.root
        for part in [p for p in path.split("/") if p]:
            nxt = cur.child(part)
            if nxt is None:
                if not create:
                    return None
                nxt = Node(part, cur)
                cur.children.append(nxt)
            cur = nxt
        return cur

    def ensure_node(self, path):
        return self.node(path, create=True)

    def get(self, path, prop):
        n = self.node(path)
        return None if n is None else n.get(prop)

    def set(self, path, prop, value):
        self.ensure_node(path).set(prop, value)

    def delete(self, path, prop):
        n = self.node(path)
        if n is not None:
            n.delete(prop)

    def tobytes(self, slack=8192):
        # collect string table in stable order
        names = []
        seen = set()

        def collect(node):
            for name, _ in node.props:
                if name not in seen:
                    seen.add(name)
                    names.append(name)
            for child in node.children:
                collect(child)

        collect(self.root)
        strings = bytearray()
        offsets = {}
        for name in names:
            offsets[name] = len(strings)
            strings += name.encode("utf-8") + b"\0"
        while len(strings) % 4:
            strings += b"\0"

        body = bytearray()

        def emit(node, tail_slack=0):
            body.extend(struct.pack(">I", BEGIN_NODE))
            raw = node.name.encode("utf-8") + b"\0"
            body.extend(raw)
            body.extend(b"\0" * ((4 - len(raw) % 4) % 4))
            for name, value in node.props:
                body.extend(struct.pack(">III", PROP, len(value), offsets[name]))
                body.extend(value)
                body.extend(b"\0" * ((4 - len(value) % 4) % 4))
            for child in node.children:
                emit(child)
            # NOPs must live inside the root node; dtc rejects them after END_NODE.
            body.extend(struct.pack(">I", NOP) * (tail_slack // 4))
            body.extend(struct.pack(">I", END_NODE))

        emit(self.root, slack)
        body.extend(struct.pack(">I", END))

        rsv = bytearray(self.rsvmap)
        if not rsv or rsv[-16:] != b"\0" * 16:
            rsv += b"\0" * 16
        off_rsv = 40
        off_struct = (off_rsv + len(rsv) + 7) & ~7
        off_strings = (off_struct + len(body) + 3) & ~3
        total = off_strings + len(strings)
        header = HEADER.pack(total, off_struct, off_strings, off_rsv,
                             17, 16, self.boot_cpuid, len(strings), len(body))
        out = bytearray(MAGIC)
        out.extend(header)
        out.extend(b"\0" * (off_rsv - len(out)))
        out.extend(rsv)
        out.extend(b"\0" * (off_struct - len(out)))
        out.extend(body)
        out.extend(b"\0" * (off_strings - len(out)))
        out.extend(strings)
        return bytes(out)

    def write(self, path, slack=8192):
        with open(path, "wb") as f:
            f.write(self.tobytes(slack))
