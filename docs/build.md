# Build

With Docker, this builds all six apps into `build/` and runs the host parser
tests. The image is built on first use.

```sh
./build-docker.sh
```

Without Docker you need `arm-none-eabi-gcc`, Python 3 and bash:

```sh
python3 tools/fetch_sdk.py   # downloads the F4HWN app SDK into sdk/
./build.sh                   # writes build/TPMSEU.app, TPMSNA.app, TPMSEU2.app, TPMSNA2.app, TPMSDBG.app, TPMSCAP.app
```

- Build one app: `./build.sh eu`, `na`, `eu2`, `na2`, `dbg` or `cap`.
- Set `PYTHON` if Python 3 is not called `python3`, for example
  `PYTHON=python ./build.sh`.
- Run the host parser tests (no radio needed): `./test/run_tests.sh`.
- Redraw the screen pictures in `docs/`: `python3 tools/mock_lcd.py`.
- The release version is `APP_VER` in `build.sh`.

Code sizes in v1.3.2 (each app must fit the 4 KB app space): `TPMS EU` 4088 B,
`TPMS NA` 4088 B, `TPMS EU2` 3756 B, `TPMS NA2` 3824 B, `TPMS DBG` 4068 B,
`TPMS CAP` 4096 B.

## See also

- [README](../README.md): the list apps, keys and screen.
- [How it works](how-it-works.md): signal path and packet checks.
