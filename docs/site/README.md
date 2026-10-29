# Website maintenance

GitHub Pages source is in `docs/`. The site uses local assets, theme galleries and a Zune walkthrough. Screenshots illustrate their capture revision; they do not establish every detail of the 3.2.2 UI. Update version links and images from verified release material.

Regenerate the 28 actual LVGL screenshots in a disposable Linux host container:

```sh
make -C ui CROSS= host-render -j4
python3 tools/render-zune-site.py --allow-host-fixtures
```

Requires Pillow. The host harness writes `/usr/data/fiio/db/song.db`; never run it on a player. Artwork is synthetic geometric illustration, not third-party album covers. Immersive is captured with its real transport overlay open. All UI colour changes are set before theme initialization.
