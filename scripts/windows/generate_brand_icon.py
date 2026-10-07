"""Package the approved LumaLive raster master into Windows icon resources.

Requires Pillow. No redraw or design changes: the original alpha and artwork
are preserved, with standard resampling only for operating-system icon sizes.
"""
from pathlib import Path
from PIL import Image, ImageDraw
root = Path(__file__).resolve().parents[2]
assets = root / "LumaLive/client/ui/studio-preview/assets"
source = Image.open(assets / "lumalive-master.png").convert("RGBA")
assert source.width == source.height and source.width >= 256
assert source.getextrema()[3][0] == 0, "Master must retain transparent corners"
sizes = (16, 20, 24, 32, 40, 48, 64, 128, 256)
image = source.resize((256, 256), Image.Resampling.LANCZOS)
image.save(assets / "lumalive.png")
image.save(assets / "lumalive.ico", sizes=[(v, v) for v in sizes])
icon = Image.open(assets / "lumalive.ico")
assert icon.ico.sizes() == {(v, v) for v in sizes}
# Review artifact only: show real-size taskbar variants on light and dark surfaces.
board = Image.new("RGB", (640, 230), "#101720")
draw = ImageDraw.Draw(board)
draw.rectangle((0, 115, 640, 230), fill="#f0f2f5")
for row, color in ((0, "#cbd7e5"), (115, "#364354")):
    x = 24
    for size in sizes[:7]:
        variant = icon.ico.getimage((size, size)).convert("RGBA")
        assert variant.getextrema()[3][0] == 0
        board.paste(variant, (x, row + 18), variant)
        draw.text((x, row + 90), str(size) + " px", fill=color)
        x += 84
output = root / "output/icon-review"
output.mkdir(parents=True, exist_ok=True)
board.save(output / "lumalive-sizes.png")
print("PASS: packaged nine ICO sizes with transparency; size sheet in output/icon-review")
