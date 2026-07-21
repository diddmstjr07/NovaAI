"""Web page fetching and text extraction for Nova Browser.

NovaOS has no HTML renderer, so the bridge reduces a page to readable text plus
a list of followable links. `Page` is the structured result; `page_reply`
serialises it into the guest's PAGE / BODY / LINK / PAGEEND frame.
"""

import urllib.parse
from dataclasses import dataclass, field
from html.parser import HTMLParser

from . import net, wire

MAX_WEB_BYTES = 256 * 1024
TEXT_CONTENT_TYPES = {"text/html", "application/xhtml+xml"}
BLOCK_TAGS = {"p", "div", "article", "section", "main", "h1", "h2", "h3", "li", "br"}
HIDDEN_TAGS = {"script", "style", "noscript", "svg"}
SKIP_SCHEMES = ("#", "javascript:", "mailto:", "data:")


@dataclass
class Link:
    label: str
    url: str


@dataclass
class Page:
    status: int
    url: str
    title: str
    body: str
    links: list[Link] = field(default_factory=list)

    @property
    def failed(self) -> bool:
        return self.status == 0


class TextPageParser(HTMLParser):
    """Extract title, readable text and anchors in a single pass."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.hidden_depth = 0
        self.in_title = False
        self.title_parts: list[str] = []
        self.text_parts: list[str] = []
        # Anchor text is also appended to text_parts so the body still reads
        # naturally; the href is kept separately for the link list.
        self.anchor_href: str | None = None
        self.anchor_parts: list[str] = []
        self.raw_links: list[tuple[str, str]] = []

    def handle_starttag(self, tag: str, attrs) -> None:
        if tag in HIDDEN_TAGS:
            self.hidden_depth += 1
        elif tag == "title" and not self.hidden_depth:
            self.in_title = True
        elif tag == "a" and not self.hidden_depth:
            href = next((value for name, value in attrs if name == "href"), None)
            if href:
                self.anchor_href = href
                self.anchor_parts = []
        elif tag in BLOCK_TAGS:
            self.text_parts.append(" ")

    def handle_endtag(self, tag: str) -> None:
        if tag in HIDDEN_TAGS and self.hidden_depth:
            self.hidden_depth -= 1
        elif tag == "title":
            self.in_title = False
        elif tag == "a" and self.anchor_href is not None:
            self.raw_links.append(
                (wire.compact_text("".join(self.anchor_parts)), self.anchor_href))
            self.anchor_href = None
            self.anchor_parts = []

    def handle_data(self, data: str) -> None:
        if self.hidden_depth:
            return
        if self.in_title:
            self.title_parts.append(data)
            return
        if self.anchor_href is not None:
            self.anchor_parts.append(data)
        self.text_parts.append(data)


def resolve_links(base_url: str, raw_links: list[tuple[str, str]]) -> list[Link]:
    """Turn raw hrefs into absolute, deduplicated, followable http(s) links."""
    resolved: list[Link] = []
    seen: set[str] = set()
    for label, href in raw_links:
        href = href.strip()
        if not href or href.startswith(SKIP_SCHEMES):
            continue
        try:
            absolute = urllib.parse.urljoin(base_url, href)
        except ValueError:
            continue
        parsed = urllib.parse.urlsplit(absolute)
        if parsed.scheme not in {"http", "https"} or not parsed.hostname:
            continue
        # Fragments never change what the bridge would fetch.
        absolute = urllib.parse.urlunsplit(parsed._replace(fragment=""))
        if len(absolute) > wire.MAX_LINK_URL or absolute in seen:
            continue
        seen.add(absolute)
        text = wire.compact_text(label)[:wire.MAX_LINK_LABEL]
        if not text:
            text = (parsed.path.rsplit("/", 1)[-1] or parsed.hostname)[:wire.MAX_LINK_LABEL]
        resolved.append(Link(text, absolute))
        if len(resolved) >= wire.MAX_LINKS:
            break
    return resolved


def fetch(url: str) -> Page:
    """Fetch a URL and reduce it to a `Page`. Never raises; failures become
    a Page with status 0 and the error in `body`."""
    try:
        with net.open_url(url, timeout=20,
                          accept="text/html,text/plain;q=0.9,*/*;q=0.1") as response:
            final_url = net.validate_public_url(response.geturl())
            content_type = response.headers.get_content_type()
            if not (content_type.startswith("text/") or content_type in TEXT_CONTENT_TYPES):
                raise ValueError(f"Unsupported content type: {content_type}")
            raw = response.read(MAX_WEB_BYTES + 1)[:MAX_WEB_BYTES]
            encoding = response.headers.get_content_charset() or "utf-8"
            document = raw.decode(encoding, "replace")
            status = getattr(response, "status", 200)

        hostname = urllib.parse.urlsplit(final_url).hostname
        if content_type in TEXT_CONTENT_TYPES:
            parser = TextPageParser()
            parser.feed(document)
            title = wire.compact_text(" ".join(parser.title_parts)) or hostname
            body = wire.compact_text(" ".join(parser.text_parts))
            links = resolve_links(final_url, parser.raw_links)
        else:
            title = hostname or "Text document"
            body = wire.compact_text(document)
            links = []
        return Page(status, final_url, title or "Untitled", body or "Empty document", links)
    except net.NETWORK_ERRORS as error:
        return Page(0, url, "Request failed", str(error), [])


def page_reply(page: Page) -> wire.Reply:
    """Serialise a Page as PAGE / BODY* / LINK* / PAGEEND.

    PAGEEND lets the guest commit the page only once every chunk has arrived,
    so a half-delivered page never replaces what is on screen.
    """
    safe_url = wire.compact_text(page.url)[:220]
    safe_title = wire.compact_text(page.title)[:100]
    text = wire.compact_text(page.body)[:wire.MAX_PAGE_TEXT]

    header = f"PAGE {page.status}\t{safe_title}\t{safe_url}\t"
    lines = wire.chunk_text(text, prefix="BODY ", first_prefix=header)
    lines.extend(f"LINK {link.label}\t{link.url}" for link in page.links)
    lines.append("PAGEEND")
    return wire.Reply.many(lines)
