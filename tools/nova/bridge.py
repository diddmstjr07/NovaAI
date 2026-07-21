"""The NovaOS bridge server and its extension points.

Two registries live here:

* **commands** — verbs the guest kernel sends over TCP. Adding one requires
  matching kernel support to actually send it.
* **AI functions** — host-side callables exposed to a model as tools. These
  need no kernel change and are the fastest way to add new capability.

Example::

    from nova import NovaBridge, Reply

    bridge = NovaBridge()

    @bridge.command("PING")
    def ping(session, argument):
        return Reply.line("ANSWER", f"pong {argument}")

    bridge.serve_forever()
"""

import inspect
import socketserver
import traceback
from typing import Callable, Iterable, Iterator

from . import wire

HOST = "0.0.0.0"
DEFAULT_PORT = 7780

# A handler returns nothing, one Reply, or an iterable/generator of Replies.
# Generators stream: each Reply is flushed to the guest as it is produced.
HandlerResult = None | wire.Reply | Iterable[wire.Reply]
Handler = Callable[["Session", str], HandlerResult]

_PYTHON_TO_JSON = {str: "string", int: "integer", float: "number", bool: "boolean"}


class Session:
    """One connected guest. Handlers may keep per-connection state here."""

    def __init__(self, bridge: "NovaBridge", peer: str) -> None:
        self.bridge = bridge
        self.peer = peer
        self.state: dict = {}


class AIFunction:
    """A host-side callable exposed to a model as a tool."""

    def __init__(self, function: Callable, name: str, description: str,
                 parameters: dict | None) -> None:
        self.function = function
        self.name = name
        self.description = description
        self.parameters = parameters or _derive_parameters(function)

    def schema(self) -> dict:
        """OpenAI-style function schema."""
        return {
            "type": "function",
            "name": self.name,
            "description": self.description,
            "parameters": {
                "type": "object",
                "properties": self.parameters,
                "required": list(self.parameters),
            },
        }

    def __call__(self, **kwargs):
        return self.function(**kwargs)


def _derive_parameters(function: Callable) -> dict:
    """Build a JSON schema from type hints so simple tools need no boilerplate."""
    properties = {}
    for name, parameter in inspect.signature(function).parameters.items():
        json_type = _PYTHON_TO_JSON.get(parameter.annotation, "string")
        properties[name] = {"type": json_type}
    return properties


class NovaBridge:
    """Line-protocol server for the guest, plus an AI tool registry."""

    def __init__(self, host: str = HOST, port: int = DEFAULT_PORT) -> None:
        self.host = host
        self.port = port
        self.commands: dict[str, Handler] = {}
        self.ai_functions: dict[str, AIFunction] = {}

    # -- guest commands ---------------------------------------------------

    def command(self, verb: str) -> Callable[[Handler], Handler]:
        """Register a handler for a guest verb.

        Matches the line exactly (argument is ``""``) or as ``VERB argument``.
        """
        def register(handler: Handler) -> Handler:
            self.commands[verb] = handler
            return handler
        return register

    def dispatch(self, session: Session, line: str) -> Iterator[wire.Reply]:
        """Resolve a guest line to its handler and yield the replies."""
        handler, argument = self._resolve(line)
        if handler is None:
            return
        try:
            result = handler(session, argument)
        except Exception:                      # a broken handler must not drop the guest
            traceback.print_exc()
            return
        if result is None:
            return
        if isinstance(result, wire.Reply):
            yield result
            return
        yield from result

    def _resolve(self, line: str) -> tuple[Handler | None, str]:
        handler = self.commands.get(line)
        if handler is not None:
            return handler, ""
        # Longest verb first so "DOWNLOAD_CHROME" is never shadowed by "DOWNLOAD".
        for verb in sorted(self.commands, key=len, reverse=True):
            prefix = verb + " "
            if line.startswith(prefix):
                return self.commands[verb], line[len(prefix):]
        return None, ""

    # -- AI functions -----------------------------------------------------

    def ai_function(self, name: str | None = None, description: str = "",
                    parameters: dict | None = None) -> Callable:
        """Expose a host-side function to a model as a callable tool.

        Parameter schema is derived from type hints unless given explicitly.
        The docstring is used when no description is passed.
        """
        def register(function: Callable) -> Callable:
            resolved = name or function.__name__
            self.ai_functions[resolved] = AIFunction(
                function, resolved,
                description or (function.__doc__ or "").strip(),
                parameters)
            return function
        return register

    def tool_schemas(self) -> list[dict]:
        """Every registered AI function, ready to pass to a model API."""
        return [tool.schema() for tool in self.ai_functions.values()]

    def call_ai_function(self, name: str, arguments: dict):
        """Invoke a registered tool by name."""
        tool = self.ai_functions.get(name)
        if tool is None:
            raise KeyError(f"Unknown AI function: {name}")
        return tool(**arguments)

    # -- serving ----------------------------------------------------------

    def serve_forever(self, banner: str | None = None) -> None:
        bridge = self

        class RequestHandler(socketserver.StreamRequestHandler):
            def handle(self) -> None:
                session = Session(bridge, self.client_address[0])
                print(f"NovaOS connected from {session.peer}", flush=True)
                for raw_line in self.rfile:
                    line = raw_line.decode("ascii", "replace").strip()
                    for reply in bridge.dispatch(session, line):
                        self.wfile.write(reply.encode())
                        self.wfile.flush()

        class Server(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        if banner:
            print(banner, flush=True)
        try:
            with Server((self.host, self.port), RequestHandler) as server:
                server.serve_forever()
        except KeyboardInterrupt:
            pass
