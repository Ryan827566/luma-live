# LumaLive application icon

The active artwork is `lumalive-master.png`, generated with the built-in image
tool on 2026-10-05. Two joined L-shaped light bands frame an open communication
space, replacing the old collage of a letter, play triangle and small nodes.
The dark tile and blue/cyan palette match the native workspace.

`lumalive.png` and `lumalive.ico` are derived runtime assets. Rebuild them with
`python scripts/windows/generate_brand_icon.py` in an environment with Pillow.
The ICO includes 16, 20, 24, 32, 40, 48, 64, 128 and 256 pixel variants.
`app.rc` embeds this ICO as resource 101. `lumalive-v1.svg` is an archived earlier
concept, not the master for the active icon. No vector master of v2 is claimed.

The size review sheet is written to `output/icon-review/lumalive-sizes.png`.
The source image is copied into the repository rather than referenced from the
Codex generated-images cache. Windows may cache an old icon for existing pins.
