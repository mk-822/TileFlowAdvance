"""Bake the browser version's 3D belt tiles for GBA. The ROM uses the baked sheet."""

import argparse
import http.server
import io
import json
import struct
import subprocess
import tempfile
import threading
from pathlib import Path
from urllib.parse import urlsplit

from PIL import Image

from generate_graphics import OUT, SPRITE_COLORS, palette

SIZES = 10
POSES = 40
TILE_WIDTH = 32
TILE_HEIGHT = 16
SCALE = 4

CAPTURE_PAGE = """<!doctype html>
<meta charset="utf-8">
<script type="importmap">
{"imports":{"three":"/three/build/three.module.js","three/addons/":"/three/examples/jsm/"}}
</script>
<div id="stage" style="position:absolute;left:-3000px;width:440px;height:540px"></div>
<output id="status">Waiting for capture</output>
<script type="module">
import * as THREE from 'three';
import { StageScene, beltPose, CELL, RAMP_ANGLE, RAMP_PITCH } from '/src/scene.js';
import { BELT_CELLS, BELT_LENGTH, BELT_STEP, TILES } from '/src/game.js';

const [sizes, poses, width, height, scale] = CAPTURE_SETTINGS;
const status = document.querySelector('#status');
try {
  // Use a neutral face outline so the same capture works with each GBA palette.
  TILES[0].symbol = '';
  const stage = new StageScene(document.querySelector('#stage'));
  stage.observer.disconnect();
  stage.renderer.setPixelRatio(1);
  stage.renderer.setSize(width * scale, height * scale, false);

  const scene = new THREE.Scene();
  for (const object of stage.scene.children) {
    if (object.isLight) scene.add(object.clone());
  }
  const tile = {id: 1, color: 0, lane: 2};
  const part = stage.getPart(tile);
  for (const child of [...part.children]) {
    if (child.isSprite) part.remove(child);
  }
  scene.add(part);

  const atlas = document.createElement('canvas');
  atlas.width = sizes * width * scale;
  atlas.height = poses * height * scale;
  const ctx = atlas.getContext('2d');
  const axis = new THREE.Vector3(1, 0, 0);
  const cell = BELT_CELLS - 4;
  for (let size = 0; size < sizes; size++) {
    const depth = (size + .5) / sizes;
    const lanePixels = 13.6 + 8.8 * depth;
    // Match the derivative of the GBA belt projection at this depth.
    const zPixels = (43 + 56 * depth) / (BELT_CELLS * RAMP_PITCH);
    const yPixels = lanePixels / CELL * .74;
    const xPixels = lanePixels / CELL;
    const verticalPixels = Math.hypot(yPixels, zPixels);
    const elevation = Math.atan2(zPixels, yPixels);
    const halfWidth = width / (2 * xPixels);
    const halfHeight = height / (2 * verticalPixels);
    const camera = new THREE.OrthographicCamera(-halfWidth, halfWidth, halfHeight, -halfHeight, .1, 20);
    const look = new THREE.Vector3(0, Math.cos(elevation), -Math.sin(elevation))
        .applyAxisAngle(axis, RAMP_ANGLE).multiplyScalar(4 / verticalPixels);
    camera.position.set(0, 8 * Math.sin(elevation), 8 * Math.cos(elevation));
    camera.position.applyAxisAngle(axis, RAMP_ANGLE).add(look);
    camera.lookAt(look);

    for (let index = 0; index < poses; index++) {
      const distance = BELT_LENGTH - (cell + index / poses) * BELT_STEP;
      const pose = beltPose(tile.lane, distance, false);
      const anchor = beltPose(tile.lane, distance, true);
      part.position.copy(pose.position).sub(anchor.position);
      part.rotation.x = pose.rotation;
      stage.renderer.clear();
      stage.renderer.render(scene, camera);
      ctx.drawImage(stage.renderer.domElement, size * width * scale, index * height * scale);
    }
  }
  const image = await new Promise(resolve => atlas.toBlob(resolve));
  const response = await fetch('/atlas', {method: 'POST', body: image});
  if (!response.ok) throw new Error('Could not save the atlas');
  status.textContent = 'Captured browser tile poses';
} catch (error) {
  status.textContent = String(error);
}
</script>
"""


def capture(project, chromium):
    page = CAPTURE_PAGE.replace("CAPTURE_SETTINGS", json.dumps([SIZES, POSES, TILE_WIDTH, TILE_HEIGHT, SCALE]))
    atlas = None

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            url = urlsplit(self.path).path
            if url == "/":
                self.send_file(page.encode(), "text/html")
                return
            if url.startswith("/src/"):
                directory = project / "src"
                asset = (project / url.lstrip("/")).resolve()
            elif url.startswith("/three/"):
                directory = project / "node_modules" / "three"
                asset = (project / "node_modules" / url.lstrip("/")).resolve()
            else:
                self.send_error(404)
                return
            if not asset.is_relative_to(directory) or asset.suffix != ".js" or not asset.is_file():
                self.send_error(404)
                return
            data = asset.read_bytes()
            if url == "/src/scene.js":
                data += b"\nexport { beltPose, CELL, RAMP_ANGLE, RAMP_PITCH };\n"
            self.send_file(data, "text/javascript")

        def do_POST(self):
            nonlocal atlas
            size = int(self.headers.get("Content-Length", 0))
            if self.path != "/atlas" or not 0 < size <= 8 * 1024 * 1024:
                self.send_error(400)
                return
            atlas = self.rfile.read(size)
            self.send_file(b"OK", "text/plain")

        def send_file(self, data, content_type):
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, *_):
            pass

    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            with tempfile.TemporaryDirectory(prefix="tile-flow-capture-") as profile:
                result = subprocess.run([
                    chromium, "--headless", "--no-first-run", "--disable-background-networking",
                    "--use-angle=swiftshader", "--enable-unsafe-swiftshader", f"--user-data-dir={profile}",
                    "--virtual-time-budget=20000", "--dump-dom", f"http://127.0.0.1:{server.server_port}/"
                ], capture_output=True, timeout=90, check=True)
            if atlas is None:
                status = result.stdout.decode(errors="replace").split('id="status">', 1)[-1].split("</output>", 1)[0]
                raise RuntimeError(f"Browser capture did not produce an atlas: {status}")
        finally:
            server.shutdown()
    return Image.open(io.BytesIO(atlas)).convert("RGBA")


def bake(atlas):
    sheet = Image.new("P", (TILE_WIDTH, TILE_HEIGHT * SIZES * POSES))
    palette(sheet, SPRITE_COLORS)
    for size in range(SIZES):
        for pose in range(POSES):
            x = size * TILE_WIDTH * SCALE
            y = pose * TILE_HEIGHT * SCALE
            sprite = atlas.crop((x, y, x + TILE_WIDTH * SCALE, y + TILE_HEIGHT * SCALE))
            sprite = sprite.resize((TILE_WIDTH, TILE_HEIGHT), Image.Resampling.BOX)
            pixels = []
            for red, green, blue, alpha in struct.iter_unpack("BBBB", sprite.tobytes()):
                light = (2 * red + 6 * green + 2 * blue) // 10
                if alpha < 100:
                    shade = 0
                elif light < 90:
                    shade = 4
                elif light < 141:
                    shade = 3
                elif light < 176:
                    shade = 5
                elif light < 216:
                    shade = 2
                else:
                    shade = 1
                pixels.append(shade)
            indexed = Image.new("P", (TILE_WIDTH, TILE_HEIGHT))
            indexed.putdata(pixels)
            sheet.paste(indexed, (0, (size * POSES + pose) * TILE_HEIGHT))
    sheet.save(OUT / "belt_roll.bmp")
    (OUT / "belt_roll.json").write_text(json.dumps({"type": "sprite", "height": TILE_HEIGHT}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path, help="browser Tile Flow checkout with its dependencies installed")
    parser.add_argument("chromium", help="path to the Chromium or Chrome executable")
    args = parser.parse_args()
    bake(capture(args.project.resolve(), args.chromium))
