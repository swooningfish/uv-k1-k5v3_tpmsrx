#!/usr/bin/env python3
"""Fetch the F4HWN overlay-app SDK files into sdk/.

TPMS RX builds against the app ABI headers and packer published in
https://github.com/armel/uv-k1-k5v3-firmware-custom (Apache-2.0). They are not
part of this repository: this script downloads them from a pinned release tag
and checks each one against its git blob SHA-1.

When the folder sits in App/apps/ of the firmware tree, build.sh uses the
firmware's own copies instead and this script is not needed.
"""

import hashlib
import os
import sys
import urllib.request

TAG = "v6.3.0"
BASE = "https://raw.githubusercontent.com/armel/uv-k1-k5v3-firmware-custom/%s/App/apps/" % TAG

FILES = {
    "app_api.h": "5c35078a85b45925dd0f85a947efc62f4c923467",
    "app_overlay.h": "70e09febb92d543956bafcfa46ee8dcc7052e8e3",
    "app_assets.py": "e30ee6e6afb2f52a44f1f4a83a6eee84e011cf7b",
    "pack_app.py": "9ac2f7c4f3cea69e9e4da3d6146d21212edf5964",
}

SDK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sdk")


def blob_sha1(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def main():
    os.makedirs(SDK, exist_ok=True)
    for name, sha1 in FILES.items():
        path = os.path.join(SDK, name)
        if os.path.exists(path):
            with open(path, "rb") as f:
                if blob_sha1(f.read()) == sha1:
                    continue
        with urllib.request.urlopen(BASE + name, timeout=30) as r:
            data = r.read()
        if blob_sha1(data) != sha1:
            sys.exit("SHA-1 mismatch: %s" % name)
        with open(path, "wb") as f:
            f.write(data)
    print("SDK %s: %d files ok in %s" % (TAG, len(FILES), os.path.normpath(SDK)))


if __name__ == "__main__":
    main()
