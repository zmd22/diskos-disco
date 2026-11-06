# Website maintenance

GitHub Pages serves `docs/` from `main`: Zune accent walkthrough, theme previews, galleries, video chapters.

Regenerate LVGL screenshots in a disposable Linux container (needs Pillow and DejaVu Sans):

```sh
make -C ui CROSS= host-render -j2
python3 tools/render-zune-site.py --allow-host-fixtures
```

Never run the host harness on a Disc: it overwrites `/usr/data/fiio/db/song.db` with sample data.

- Demo sleeves are original illustrations, not real covers.
- Zune/gallery captures are from 3.2.2; settings and revision are in `captures.json`.
- Braun 2.0 previews (`screens/braun2-*.webp`) use preset 14 (light Music and QS, dark Home); capture revision unrecorded.
- Site updates don't touch firmware files.
