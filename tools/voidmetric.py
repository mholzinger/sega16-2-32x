import sys
from PIL import Image
from collections import Counter
im = Image.open(sys.argv[1]).convert('RGB')
px = im.load()
w, h = im.size
c = Counter()
n = 0
for y in range(16, 120):
    for x in range(8, w - 8):
        c[px[x, y]] += 1
        n += 1
top, cnt = c.most_common(1)[0]
# void = one flat colour dominating the BG region
print(f"dominant {top} {100*cnt/n:.1f}%  distinct={len(c)}")
