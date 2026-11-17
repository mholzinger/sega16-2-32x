"""The Makefile as one text: the entry file with every `include mk/*.mk` expanded in place.

The build is split across Makefile and mk/*.mk. A tool that looks for an option
block, a flag list or a rule reads it through here and sees one file, in the order
make parses it.

    from mktext import makefile_text, makefile_parts
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_INC = re.compile(r'^-?include\s+(\S+\.mk)\s*$')


def makefile_parts(root=ROOT, game='altbeast'):
    """[(path relative to root, text)] in parse order; the entry file's own lines come
    as several chunks, one between each pair of includes."""
    out, cur = [], []
    for line in open(os.path.join(root, 'Makefile'), errors='replace').read().split('\n'):
        m = _INC.match(line)
        p = m and m.group(1).replace('$(GAME)', game)
        if p and os.path.exists(os.path.join(root, p)):
            out.append(('Makefile', '\n'.join(cur)))
            out.append((p, open(os.path.join(root, p), errors='replace').read().rstrip('\n')))
            cur = []
        else:
            cur.append(line)
    out.append(('Makefile', '\n'.join(cur)))
    return out


def makefile_text(root=ROOT, game='altbeast'):
    return '\n'.join(text for _, text in makefile_parts(root, game))
