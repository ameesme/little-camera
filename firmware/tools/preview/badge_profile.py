#!/usr/bin/env python3
"""Print PAL_BADGE_PROFILE for display.cpp: the edge of a star with filleted corners.

The star has tips at radius 1 and dips at INNER; opening then closing it with a
disc of radius FILLET rounds the tips and the dips alike. The radius is sampled
along STEPS+1 rays from a tip to the next dip and scaled to 0-255 of the tip.
Needs shapely (pip install shapely); the firmware only ever sees the numbers.
"""
import math
from shapely.geometry import LineString, Polygon

POINTS, INNER, FILLET, STEPS = 12, 0.78, 0.1, 32

pts = []
for i in range(2 * POINTS):
    r = 1 if i % 2 == 0 else INNER
    a = math.pi * i / POINTS
    pts.append((r * math.cos(a), r * math.sin(a)))
star = Polygon(pts).buffer(-FILLET, resolution=64).buffer(2 * FILLET, resolution=64).buffer(-FILLET, resolution=64)

radii = []
for k in range(STEPS + 1):
    a = (math.pi / POINTS) * k / STEPS
    hit = star.exterior.intersection(LineString([(0, 0), (2 * math.cos(a), 2 * math.sin(a))]))
    hits = [hit] if hit.geom_type == "Point" else list(hit.geoms)
    radii.append(max(math.hypot(p.x, p.y) for p in hits))
top = max(radii)
print(", ".join(str(round(r / top * 255)) for r in radii))
