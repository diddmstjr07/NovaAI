"""The capabilities NovaOS ships with today.

`install(bridge)` wires the guest verbs the current kernel knows how to send,
and registers the same features as AI functions. Anything added here becomes
available to both the OS and the model.
"""

from . import downloads, model, web, wire


def install(bridge) -> None:
    """Register the stock guest commands and AI tools on a bridge."""

    # -- guest commands, matching what ui/ui.c sends ----------------------

    @bridge.command("NOVA/1 HELLO")
    def hello(session, argument):
        """Handshake. The guest shows 'bridge connected' once this arrives."""
        return wire.Reply.raw("NOVA/1 READY")

    @bridge.command("PROMPT")
    def prompt(session, argument):
        """Nova AI question from the guest."""
        answer = wire.compact_text(model.generate(argument))[:wire.MAX_ANSWER]
        return wire.Reply.line("ANSWER", answer)

    @bridge.command("GET")
    def get(session, argument):
        """Nova Browser page request."""
        return web.page_reply(web.fetch(argument))

    @bridge.command("DOWNLOAD_CHROME")
    def download_chrome(session, argument):
        """Fetch the official Chrome .deb into the host cache."""
        return downloads.fetch_to_cache(downloads.CHROME_URL)

    @bridge.command("DOWNLOAD")
    def download(session, argument):
        """Fetch an arbitrary URL into the host cache."""
        return downloads.fetch_to_cache(argument)

    # -- AI functions, usable without any kernel change -------------------

    @bridge.ai_function(description="Fetch a web page and return its readable "
                                    "text and links.")
    def web_fetch(url: str) -> dict:
        page = web.fetch(url)
        return {
            "status": page.status,
            "url": page.url,
            "title": page.title,
            "text": page.body[:wire.MAX_PAGE_TEXT],
            "links": [{"label": link.label, "url": link.url} for link in page.links],
        }

    @bridge.ai_function(description="Download a file to the host downloads/ "
                                    "folder and report the result.")
    def download_file(url: str) -> str:
        last = ""
        for reply in downloads.fetch_to_cache(url):
            last = reply.lines[-1]
        return last
