> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Test 2.5.4-dev1 against 2.5.3

This candidate changes Display organization and avoids identified invisible visual work. Your stable 2.5.3 remains the comparison and rollback build. It does not include the shelved migration system or GIF screensaver.

## Temporary UI test

The test pack includes `mq_ui`, `mq_ui-2.5.3-rollback`, the existing `diskos-deploy.sh`, checksums and the checkpoint documents. This helper requires sshpass, ssh and md5sum on your computer. Enable Debug Mode on the Disc for its current password.

From the extracted test-pack folder:

```bash
sha256sum -c SHA256SUMS
read -rsp "Disc Debug Mode password: " DISKOS_PW; echo
export DISKOS_PW
export DISKOS_IP=192.168.0.160
bash diskos-deploy.sh mq_ui
unset DISKOS_PW
```

About should identify `2.5.4-dev1`. The helper replaces only the temporary UI and restarts mq_ui; it does not kill mq_player. Reboot returns to the flashed UI. Settings changed during testing remain saved. There is no configuration format change, so 2.5.3 can read them.

To restore 2.5.3 immediately, repeat the password/export steps and run:

```bash
bash diskos-deploy.sh mq_ui-2.5.3-rollback
```

Use your established signed-update/installer path for a permanent install after testing. This candidate does not change the shipping payload or create a release tag.

## Short functional check

1. Visit Display in Disco, Null and Squash. Confirm the same top-level categories and check that existing brightness/theme preferences survive switching themes.
2. Tap dimmed settings: each should explain why it is disabled. Confirm Null palettes, Squash backgrounds, clock/title preferences, gestures, QS items and shortcuts still work.
3. Check Ring/Braun if used, including their separate Now Playing style. Check large text and screen rotation if you use them.
4. Play music, open/close settings and QS repeatedly, then let the screen turn off. Wake it several times using both touch and the power button. Check clock, Music progress and controls update promptly.
5. Change tracks with the screen off, then check Recently Played/Most Played. History must continue recording.
6. Let a longer listening session run. Check for hangs, missing input, growing memory, visual faults and new boot-log errors.

## Efficiency comparison

Use the same album/files, brightness, output, volume, wireless state, screensaver and screen-off timeouts for both builds. Compare static Music, browsing, and fully-off playback separately. In Debug Mode, `top` can show whether the UI settles when untouched; compare like-for-like states rather than one short CPU spike.

For a memory snapshot over SSH:

```sh
for p in $(pidof mq_ui); do
  echo "mq_ui PID $p"
  awk '/VmSize|VmRSS|VmHWM/ {print}' /proc/$p/status
 done
awk '/MemTotal|MemFree|MemAvailable|Cached|SwapTotal/ {print}' /proc/meminfo
```

Some memory is reusable cache; low MemFree alone does not establish a leak. Look for continuing growth across repeated navigation after things have settled. For battery duration, use matched unplugged sessions; keep Wi-Fi either off for both or on for both. No battery gain is claimed from the host tests.

## Build and repeat host checks

Source base: 2.5.3 commit `9ba4dedfbba9d4a0ed4421cba10c3c4800f48818`.

Use a separate checkout for native and MIPS builds, because they share object paths. Repository prebuilt toolchain: release `toolchain-gcc11.2`, GCC 11.2.0 / musl 1.2.6, mipsel-n2008-musl-cross. Build with:

```sh
make -C ui CROSS=/absolute/path/mipsel-n2008-musl-cross/bin/mipsel-linux-musl- -j1 mq_ui
```

In the separate native checkout:

```sh
make -C ui CROSS= -j1 host-render
python3 ui/tests/display_pass_test.py
python3 ui/tests/history_test.py
python3 ui/tests/theme_compat_test.py
make -C ui CROSS= check-theme
```

Native tests run real LVGL widgets with simulated input and radio/player fixtures. They do not emulate hardware power transitions or measure battery life.
