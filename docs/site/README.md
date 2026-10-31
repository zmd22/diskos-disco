# Website maintenance

GitHub Pages serves `docs/` from `main`. The static site has a Zune accent walkthrough, theme previews, collection galleries and video chapters.

Regenerate the actual LVGL screenshots in a disposable Linux host container:

```sh
make -C ui CROSS= host-render -j2
python3 tools/render-zune-site.py --allow-host-fixtures
```

Requires Pillow and DejaVu Sans. Never run the host harness on a player: it replaces `/usr/data/fiio/db/song.db` with sample music and history. Geometric demo sleeves are original illustrations, not official artist covers. The original Zune/gallery capture settings and source revision are recorded in `captures.json`; those retained images are from 3.2.2. Braun previews are listed separately because their original capture revision was not recorded. No device firmware files are changed by the website refresh.

The Braun 2.0 previews (`screens/braun2-*.webp`) come from the same host renderer with preset 14 (light Music and Quick Settings, dark Home) and an original illustrated sleeve.
