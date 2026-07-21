"""Model access for Nova AI.

The API key stays on the host and is never written into the disk image, so the
guest can ask questions without holding any credential.

Provider selection is environment-driven so a different backend can be dropped
in without touching the bridge or the kernel.
"""

import json
import os
import urllib.error
import urllib.request

DEFAULT_INSTRUCTIONS = (
    "You are Nova AI inside a small educational operating system. "
    "Answer concisely in plain text without Markdown tables."
)
OFFLINE_NOTICE = ("AI bridge is connected. Set OPENAI_API_KEY on the Mac "
                  "for model responses.")


def model_name() -> str:
    return os.environ.get("NOVA_AI_MODEL", "gpt-5.6-sol")


def is_configured() -> bool:
    return bool(os.environ.get("OPENAI_API_KEY"))


def describe() -> str:
    """Short mode string for the startup banner."""
    return f"web + OpenAI model {model_name()}" if is_configured() else "web + offline AI"


def generate(prompt: str, instructions: str = DEFAULT_INSTRUCTIONS,
             timeout: int = 90) -> str:
    """Ask the model a question. Returns an explanatory string on failure
    rather than raising, so the guest always receives an ANSWER line."""
    api_key = os.environ.get("OPENAI_API_KEY")
    if not api_key:
        return OFFLINE_NOTICE

    payload = json.dumps({
        "model": model_name(),
        "reasoning": {"effort": "low"},
        "instructions": instructions,
        "input": prompt,
    }).encode("utf-8")
    request = urllib.request.Request(
        "https://api.openai.com/v1/responses",
        data=payload,
        method="POST",
        headers={"Authorization": f"Bearer {api_key}",
                 "Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            document = json.load(response)
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError) as error:
        return f"OpenAI request failed: {error}"

    parts: list[str] = []
    for item in document.get("output", []):
        if item.get("type") != "message":
            continue
        for content in item.get("content", []):
            if content.get("type") == "output_text" and content.get("text"):
                parts.append(content["text"])
    return "\n".join(parts) or "The model returned no text output."
