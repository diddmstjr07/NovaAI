"""Nova bridge API.

The host-side Python layer NovaOS talks to. Add new behaviour by registering
commands (guest verbs) or AI functions (model tools) on a `NovaBridge`.

    from nova import NovaBridge, Reply, capabilities

    bridge = NovaBridge()
    capabilities.install(bridge)      # web, downloads, Nova AI

    @bridge.ai_function(description="Return the host time.")
    def host_time() -> str:
        import datetime
        return datetime.datetime.now().isoformat()

    bridge.serve_forever()

See `tools/README_API.md` for the full guide.
"""

from .bridge import AIFunction, NovaBridge, Session
from .wire import Reply, compact_text
from .web import Link, Page
from . import capabilities, downloads, model, net, web, wire

__all__ = [
    "NovaBridge", "Session", "AIFunction",
    "Reply", "compact_text",
    "Page", "Link",
    "capabilities", "downloads", "model", "net", "web", "wire",
]
