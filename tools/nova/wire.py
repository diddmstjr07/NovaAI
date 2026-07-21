"""NovaOS line protocol primitives.

The guest kernel parses a fixed ASCII line protocol, so every byte produced here
is constrained by what `ui/ui.c` can consume:

* one message per line, terminated by ``\\n``
* fields inside a line are separated by TAB
* ASCII only; anything else is replaced
* a single line must stay under the guest's wire buffer (2048 bytes)

Changing these limits requires rebuilding the kernel. Treat them as a contract.
"""

# Longest body chunk the bridge will emit. "BODY " + MAX_WIRE_LINE = 885 bytes,
# which must remain well below the guest's 2048-byte reassembly buffer.
MAX_WIRE_LINE = 880
# Total page text delivered to the guest, bounded by its 8 KiB page buffer.
MAX_PAGE_TEXT = 7000
MAX_LINKS = 24
MAX_LINK_LABEL = 48
MAX_LINK_URL = 200
# Single-line payloads (ANSWER, DOWNLOAD) the guest truncates on its side.
MAX_ANSWER = 900
MAX_EVENT_LINE = 950


def compact_text(text: str) -> str:
    """Collapse all whitespace runs into single spaces.

    The guest has no line-breaking logic beyond its own word wrap, so incoming
    text must not contain newlines or tabs that would corrupt field parsing.
    """
    return " ".join(text.replace("\r", " ").replace("\n", " ").replace("\t", " ").split())


class Reply:
    """One or more protocol lines to send to the guest.

    Handlers return these instead of writing to a socket, which keeps them
    testable and prevents accidentally emitting malformed frames.
    """

    __slots__ = ("lines",)

    def __init__(self, lines: list[str]) -> None:
        self.lines = lines

    @classmethod
    def raw(cls, line: str) -> "Reply":
        """A literal line, used for fixed tokens such as ``NOVA/1 READY``."""
        return cls([line])

    @classmethod
    def line(cls, verb: str, text: str = "") -> "Reply":
        """``VERB text`` — the space is always emitted, even for empty text."""
        return cls([f"{verb} {text}"])

    @classmethod
    def fields(cls, *fields: str, limit: int = MAX_EVENT_LINE) -> "Reply":
        """TAB-separated fields on one line, truncated to ``limit``."""
        return cls(["\t".join(fields)[:limit]])

    @classmethod
    def many(cls, lines: list[str]) -> "Reply":
        """Several lines delivered as one frame, e.g. PAGE / BODY / PAGEEND."""
        return cls(list(lines))

    def encode(self) -> bytes:
        return ("\n".join(self.lines) + "\n").encode("ascii", "replace")

    def __repr__(self) -> str:
        return f"Reply({self.lines!r})"


def chunk_text(text: str, prefix: str, first_prefix: str = "") -> list[str]:
    """Split text across lines so the guest can concatenate them verbatim.

    ``first_prefix`` carries the header fields of the opening line; the
    remainder is emitted under ``prefix``. Splitting mid-word is intentional and
    lossless because the guest appends chunks without inserting separators.
    """
    if first_prefix:
        available = max(80, MAX_WIRE_LINE - len(first_prefix))
        lines = [first_prefix + text[:available]]
        remaining = text[available:]
    else:
        lines = []
        remaining = text
    span = MAX_WIRE_LINE
    while remaining:
        lines.append(prefix + remaining[:span])
        remaining = remaining[span:]
    return lines
