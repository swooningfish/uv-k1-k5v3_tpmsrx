# Build

With Docker, this builds all seven apps into `build/` and runs the host parser
tests. The image is built on first use.

```sh
./build-docker.sh
```

Without Docker you need `arm-none-eabi-gcc`, Python 3 and bash:

```sh
python3 tools/fetch_sdk.py   # downloads the F4HWN app SDK into sdk/
./build.sh                   # writes build/TPMSEU.app, TPMSNA.app, TPMSEU2.app, TPMSNA2.app, TPMSEU3.app, TPMSDBG.app, TPMSCAP.app
```

- Build one app: `./build.sh eu`, `na`, `eu2`, `na2`, `eu3`, `dbg` or `cap`.
- Set `PYTHON` if Python 3 is not called `python3`, for example
  `PYTHON=python ./build.sh`.
- Run the host parser tests (no radio needed): `./test/run_tests.sh`.
- Redraw the screen pictures in `docs/`: `python3 tools/mock_lcd.py`.
- The release version is `APP_VER` in `build.sh`.

Code sizes now (each app must fit the 4 KB app space, `.bss` included):
`TPMS EU` 4084 B, `TPMS NA` 4080 B, `TPMS EU2` 3784 B, `TPMS NA2` 3856 B,
`TPMS EU3` 3760 B, `TPMS DBG` 3348 B, `TPMS CAP` 3840 B.

Screen text lives in the app's assets, not its code: `gen_assets.py` for the
list apps, `gen_dbg_assets.py` and `gen_cap_assets.py` for TPMS DBG and TPMS CAP.
The apps read it with `asset_read`, so a new string costs no overlay bytes.

## See also

- [README](../README.md): the list apps, keys and screen.
- [How it works](how-it-works.md): signal path and packet checks.
