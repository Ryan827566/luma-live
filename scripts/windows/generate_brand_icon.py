"""Rasterize the original LumaLive mark at Windows icon sizes (requires Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw
root = Path(__file__).resolve().parents[2]
assets = root / "LumaLive/client/ui/studio-preview/assets"
scale = 4
image = Image.new("RGBA", (256*scale, 256*scale))
draw = ImageDraw.Draw(image)
def box(values): return tuple(round(v*scale) for v in values)
def line(points, color, width):
    draw.line([box(p) for p in points], fill=color, width=width*scale, joint="curve")
    radius=width/2
    for x,y in points: draw.ellipse(box((x-radius,y-radius,x+radius,y+radius)),fill=color)
draw.rounded_rectangle(box((0,0,256,256)), radius=52*scale, fill="#101720")
line([(58,54),(58,182),(145,182)], "#2099ee",28)
line([(111,56),(111,132)],"#91d4ff",22)
draw.polygon([box(p) for p in [(151,68),(202,102),(151,136)]],fill="#edf7ff")
line([(171,181),(193,160),(208,138)],"#3d6c88",5)
for x,y,r,color in [(171,181,9,"#2099ee"),(193,160,9,"#91d4ff"),(208,138,7,"#f28d62")]:draw.ellipse(box((x-r,y-r,x+r,y+r)),fill=color)
image=image.resize((256,256),Image.Resampling.LANCZOS)
image.save(assets/"lumalive.png")
image.save(assets/"lumalive.ico",sizes=[(v,v) for v in (16,24,32,48,64,256)])
print("Generated PNG and six-size ICO")
