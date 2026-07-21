#!/usr/bin/env python3
"""NovaOS host bridge — entry point.

Serves the guest's line protocol over TCP: HTTPS web access, downloads and the
Nova AI model. The API key stays on the host, so no secret is stored in the
disk image.

The implementation lives in the `nova` package; this file only wires it up.
To add features, register them on the bridge — see tools/README_API.md.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from nova import NovaBridge, capabilities, model  # noqa: E402


def build_bridge() -> NovaBridge:
    bridge = NovaBridge(port=int(os.environ.get("NOVA_BRIDGE_PORT", "7780")))
    capabilities.install(bridge)
    return bridge


if __name__ == "__main__":
    bridge = build_bridge()
    bridge.serve_forever(
        banner=f"Nova internet bridge listening on {bridge.host}:{bridge.port} "
               f"({model.describe()})")
