"""Outbound network safety for the bridge.

The bridge runs on the host with full network access while the guest is
untrusted, so every URL the guest supplies is validated before a request is
made — including after each redirect.
"""

import ipaddress
import socket
import urllib.error
import urllib.parse
import urllib.request

USER_AGENT = "Mozilla/5.0 (NovaOS; NovaBrowser/1.0)"
MAX_URL_LENGTH = 240


def validate_public_url(url: str) -> str:
    """Return the URL if it resolves only to public addresses, else raise.

    Blocks private and loopback ranges so the guest cannot use the bridge to
    reach services on the host or the local network.
    """
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username:
        raise ValueError("Only public http:// and https:// URLs are supported")
    if len(url) > MAX_URL_LENGTH:
        raise ValueError("URL is too long")
    try:
        default_port = 443 if parsed.scheme == "https" else 80
        addresses = socket.getaddrinfo(parsed.hostname, parsed.port or default_port,
                                       type=socket.SOCK_STREAM)
    except socket.gaierror as error:
        raise ValueError(f"DNS lookup failed: {error}") from error
    for address in addresses:
        ip = ipaddress.ip_address(address[4][0])
        if not ip.is_global:
            raise ValueError("Local and private network addresses are blocked")
    return urllib.parse.urlunsplit(parsed)


class SafeRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Re-validate every redirect target; a public URL may redirect inward."""

    def redirect_request(self, request, fp, code, message, headers, new_url):
        validate_public_url(new_url)
        return super().redirect_request(request, fp, code, message, headers, new_url)


def open_url(url: str, timeout: int = 20, accept: str | None = None):
    """Open a validated URL through the redirect-checking opener."""
    requested = validate_public_url(url)
    headers = {"User-Agent": USER_AGENT}
    if accept:
        headers["Accept"] = accept
    request = urllib.request.Request(requested, headers=headers)
    opener = urllib.request.build_opener(SafeRedirectHandler())
    return opener.open(request, timeout=timeout)


# Exceptions worth catching around any bridge network call.
NETWORK_ERRORS = (ValueError, urllib.error.URLError, urllib.error.HTTPError, TimeoutError)
