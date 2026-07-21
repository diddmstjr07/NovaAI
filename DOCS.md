# NovaOS Python API

호스트(Mac)에서 NovaOS와 통신하는 파이썬 계층 문서입니다.
앞으로의 기능과 AI Function은 **커널을 건드리지 않고 여기에 파이썬으로 추가**합니다.

- 대상: `tools/nova/` 패키지
- 진입점: `tools/ai_bridge.py`
- 실행: `python3 tools/ai_bridge.py` (또는 `./build.sh`가 자동 실행)

---

## 목차

1. [구조](#1-구조)
2. [빠른 시작](#2-빠른-시작)
3. [두 가지 확장점](#3-두-가지-확장점)
4. [AI 함수 추가](#4-ai-함수-추가)
5. [게스트 명령 추가](#5-게스트-명령-추가)
6. [Reply — 응답 만들기](#6-reply--응답-만들기)
7. [프로토콜 제약](#7-프로토콜-제약)
8. [모듈 레퍼런스](#8-모듈-레퍼런스)
9. [현재 탑재된 기능](#9-현재-탑재된-기능)
10. [게스트 없이 테스트](#10-게스트-없이-테스트)
11. [아직 없는 것](#11-아직-없는-것)

---

## 1. 구조

```
tools/
├── ai_bridge.py          진입점 (25줄)
└── nova/
    ├── __init__.py       공개 API
    ├── wire.py           라인 프로토콜 원시 요소 (Reply, 길이 제한)
    ├── bridge.py         NovaBridge — 명령/AI 함수 레지스트리, TCP 서버
    ├── net.py            URL 검증, 리다이렉트 안전 오프너
    ├── web.py            Page/Link 구조체, HTML 파싱, 링크 추출
    ├── downloads.py      스트리밍 다운로드
    ├── model.py          모델 호출
    └── capabilities.py   현재 탑재 기능 등록
```

### 전체 흐름

```
NovaOS 게스트 ──TCP 7780──> ai_bridge.py ──> NovaBridge.dispatch()
                                                    │
                                          ┌─────────┴─────────┐
                                          │                   │
                                    @command 핸들러      @ai_function
                                          │                   │
                                     Reply 프레임         모델 tool call
                                          │
                      <──ASCII 라인 프로토콜──┘ 
```

---

## 2. 빠른 시작

```python
from nova import NovaBridge, Reply, capabilities

bridge = NovaBridge()
capabilities.install(bridge)     # 웹 / 다운로드 / Nova AI

bridge.serve_forever()
```

실행:

```sh
python3 tools/ai_bridge.py
```

환경 변수:

| 변수 | 기본값 | 용도 |
|---|---|---|
| `NOVA_BRIDGE_PORT` | `7780` | 리슨 포트 |
| `NOVA_AI_MODEL` | `gpt-5.6-sol` | 모델 이름 |
| `OPENAI_API_KEY` | (없음) | 없으면 오프라인 안내문 응답 |
| `NOVA_DOWNLOAD_DIR` | `downloads/` | 다운로드 저장 위치 |

> API 키는 **호스트에만** 둡니다. 디스크 이미지에 절대 들어가지 않습니다.

---

## 3. 두 가지 확장점

| | 게스트 명령 (`@bridge.command`) | AI 함수 (`@bridge.ai_function`) |
|---|---|---|
| 누가 호출 | NovaOS 커널이 TCP로 | 모델이 tool call로 |
| 커널 수정 | **필요** (보낼 코드가 있어야 함) | **불필요** |
| 커널 재빌드 | 필요 | 불필요 |
| 용도 | OS 화면의 버튼·앱 동작 | AI 에이전트 능력 |

> **기능을 가장 빠르게 늘리는 길은 AI 함수입니다.** 파이썬만 고치면 끝입니다.

---

## 4. AI 함수 추가

타입 힌트에서 JSON 스키마가 자동 생성되고, docstring이 설명으로 쓰입니다.

```python
@bridge.ai_function(description="호스트의 현재 시각을 반환한다.")
def host_time() -> str:
    import datetime
    return datetime.datetime.now().isoformat()
```

인자가 여럿이어도 동일합니다.

```python
@bridge.ai_function(description="두 도시의 날씨를 비교한다.")
def compare_weather(city_a: str, city_b: str) -> dict:
    return {"a": city_a, "b": city_b}
```

### 타입 매핑

| 파이썬 | JSON Schema |
|---|---|
| `str` | `string` |
| `int` | `integer` |
| `float` | `number` |
| `bool` | `boolean` |
| 힌트 없음 | `string` |

### 스키마 직접 지정

자동 생성으로 부족하면 명시합니다.

```python
@bridge.ai_function(name="search", description="검색",
                    parameters={"query": {"type": "string"},
                                "limit": {"type": "integer"}})
def do_search(query, limit):
    return f"{query}/{limit}"
```

### 사용

```python
bridge.tool_schemas()                       # 모델 API에 넘길 스키마 리스트
bridge.call_ai_function("host_time", {})    # 직접 호출
```

`tool_schemas()` 출력 형태:

```python
{
    "type": "function",
    "name": "web_fetch",
    "description": "Fetch a web page and return its readable text and links.",
    "parameters": {
        "type": "object",
        "properties": {"url": {"type": "string"}},
        "required": ["url"],
    },
}
```

---

## 5. 게스트 명령 추가

핸들러 시그니처는 `(session, argument)`입니다.

```python
@bridge.command("PING")
def ping(session, argument):
    return Reply.line("ANSWER", f"pong {argument}")
```

게스트가 `PING hello\n`을 보내면 `ANSWER pong hello\n`을 받습니다.

> 이 명령이 실제로 쓰이려면 **커널이 그 줄을 보내도록** `ui/ui.c`도 고쳐야 합니다.

### 매칭 규칙

- 줄 전체가 정확히 일치 → `argument = ""`
- `VERB ` 접두사 일치 → `argument = ` 나머지
- **긴 verb 우선** — `DOWNLOAD_CHROME`이 `DOWNLOAD`에 가려지지 않습니다

### 반환 형식

| 반환 | 동작 |
|---|---|
| `None` | 응답 없음 |
| `Reply` | 한 프레임 전송 |
| 리스트 | 여러 프레임 전송 |
| **제너레이터** | **생성되는 대로 즉시 전송** (진행률 표시용) |

```python
@bridge.command("SCAN")
def scan(session, argument):
    for index in range(5):
        yield Reply.fields("DOWNLOAD", "ACTIVE", str(index * 20), "0", "scan", "")
    yield Reply.fields("DOWNLOAD", "DONE", "100", "0", "scan", "완료")
```

### 세션 상태

`session.state`는 연결마다 독립된 dict입니다.

```python
@bridge.command("COUNT")
def count(session, argument):
    session.state["n"] = session.state.get("n", 0) + 1
    return Reply.line("ANSWER", str(session.state["n"]))
```

### 예외 처리

핸들러에서 예외가 나도 **연결은 끊기지 않습니다.** 트레이스백만 호스트 콘솔에 찍히고
그 명령은 무응답 처리됩니다. 게스트가 브릿지 오류로 멈추는 일을 막기 위한 설계입니다.

---

## 6. Reply — 응답 만들기

```python
Reply.raw("NOVA/1 READY")             # NOVA/1 READY\n
Reply.line("ANSWER", "안녕")           # ANSWER 안녕\n
Reply.fields("DOWNLOAD", "DONE", …)   # 탭 구분 한 줄
Reply.many([...])                     # 여러 줄을 한 프레임으로
```

| 메서드 | 용도 |
|---|---|
| `raw(line)` | 고정 토큰 (`NOVA/1 READY`) |
| `line(verb, text)` | `VERB text` — 빈 text여도 공백은 유지 |
| `fields(*fields, limit)` | 탭 구분, `limit` 바이트로 절단 |
| `many(lines)` | PAGE/BODY/LINK/PAGEEND처럼 다중 라인 |
| `.encode()` | ASCII 바이트로 직렬화 |

---

## 7. 프로토콜 제약

**커널이 현재 프로토콜에 맞춰 컴파일되어 있습니다. 아래를 어기면 게스트가 깨집니다.**

| 제약 | 값 | 상수 |
|---|---|---|
| 한 줄 최대 | 2048 미만 (현재 최대 885) | `wire.MAX_WIRE_LINE` = 880 |
| 인코딩 | ASCII만 (그 외 `?`로 치환) | — |
| 구분자 | 줄 = `\n`, 필드 = `\t` | — |
| 본문 총량 | 7000 바이트 | `wire.MAX_PAGE_TEXT` |
| 링크 수 | 24 | `wire.MAX_LINKS` |
| 링크 라벨 / URL | 48 / 200 | `wire.MAX_LINK_LABEL` / `MAX_LINK_URL` |
| ANSWER 본문 | 900 | `wire.MAX_ANSWER` |

### 왜 885바이트인가

게스트의 와이어 버퍼는 2048바이트입니다. 한 번에 256바이트씩 읽어 개행이 나올 때까지
누적하므로, 최대 잔여분 885 + 256 = 1141 < 2047이어야 안전합니다.
**이 값을 올리려면 `ui/ui.c`의 `ai_wire_buffer`도 함께 키우고 커널을 재빌드해야 합니다.**

### 반드시 compact_text()를 통과시킬 것

본문에 `\n`이나 `\t`가 섞이면 필드 파싱이 깨집니다.

```python
from nova import compact_text
from nova import wire

Reply.line("ANSWER", compact_text(user_text)[:wire.MAX_ANSWER])
```

---

## 8. 모듈 레퍼런스

### `nova.wire`

```python
compact_text(text) -> str          # 모든 공백류를 단일 공백으로 축약
chunk_text(text, prefix, first_prefix) -> list[str]
Reply                              # 위 6절 참고
```

`chunk_text`는 본문을 여러 줄로 나눕니다. **단어 중간에서 잘려도 무손실**입니다.
게스트가 구분자 없이 그대로 이어붙이기 때문입니다.

### `nova.net`

```python
validate_public_url(url) -> str    # 공인 IP만 허용, 아니면 ValueError
open_url(url, timeout, accept)     # 리다이렉트마다 재검증하는 오프너
NETWORK_ERRORS                     # except에 쓸 예외 튜플
```

게스트는 신뢰할 수 없으므로 **사설·루프백 주소를 차단**합니다.
공인 URL이 내부로 리다이렉트하는 경우까지 막습니다.

### `nova.web`

```python
@dataclass Page:  status, url, title, body, links, failed
@dataclass Link:  label, url

fetch(url) -> Page                 # 절대 예외를 던지지 않음. 실패 시 status=0
page_reply(page) -> Reply          # PAGE / BODY* / LINK* / PAGEEND 직렬화
resolve_links(base_url, raw) -> list[Link]
```

링크 처리 규칙:

- 상대 경로 → 절대 URL 변환
- `#`, `javascript:`, `mailto:`, `data:` 제외
- fragment 제거 후 중복 제거
- 라벨이 비면 파일명 또는 호스트명으로 대체

### `nova.downloads`

```python
fetch_to_cache(url) -> Iterator[Reply]   # 진행률 이벤트를 순차 생성
event(state, percent, received, filename, detail) -> Reply
download_dir() -> Path
```

`.part` 파일에 쓰고 **예상 바이트 수가 맞을 때만** 최종 위치로 옮깁니다.
중단된 다운로드가 정상 파일처럼 남지 않습니다.

상태값: `START` / `ACTIVE` / `DONE` / `ERROR`

### `nova.model`

```python
generate(prompt, instructions, timeout) -> str   # 실패해도 문자열 반환
is_configured() -> bool
describe() -> str                                # 배너용 모드 문자열
```

실패 시 예외 대신 설명 문자열을 돌려주므로, **게스트는 항상 ANSWER를 받습니다.**

### `nova.bridge`

```python
NovaBridge(host, port)
  .command(verb)                   # 데코레이터
  .ai_function(name, description, parameters)
  .dispatch(session, line) -> Iterator[Reply]
  .tool_schemas() -> list[dict]
  .call_ai_function(name, args)
  .serve_forever(banner)

Session:  bridge, peer, state
```

---

## 9. 현재 탑재된 기능

`capabilities.install(bridge)`가 등록합니다.

### 게스트 명령

| 명령 | 응답 | 동작 |
|---|---|---|
| `NOVA/1 HELLO` | `NOVA/1 READY` | 핸드셰이크 |
| `PROMPT <질문>` | `ANSWER <답>` | Nova AI |
| `GET <url>` | `PAGE`/`BODY`*/`LINK`*/`PAGEEND` | 페이지 로드 |
| `DOWNLOAD <url>` | `DOWNLOAD` 이벤트 스트림 | 호스트로 다운로드 |
| `DOWNLOAD_CHROME` | 위와 동일 | Chrome .deb |

### AI 함수

| 이름 | 설명 |
|---|---|
| `web_fetch(url)` | 페이지 텍스트 + 링크를 구조화해 반환 |
| `download_file(url)` | 호스트 `downloads/`로 저장 |

### PAGE 프레임 예시

```
PAGE 200<TAB>Example Domain<TAB>https://example.com<TAB>Example Domain This domain is...
LINK Learn more<TAB>https://iana.org/domains/example
PAGEEND
```

`PAGEEND`가 도착해야 게스트가 화면을 갱신합니다.
덕분에 **덜 도착한 페이지가 기존 화면을 덮어쓰지 않습니다.**

---

## 10. 게스트 없이 테스트

커널을 띄우지 않고 파이썬만으로 확인할 수 있습니다.

```python
from nova import NovaBridge, capabilities
from nova.bridge import Session

bridge = NovaBridge()
capabilities.install(bridge)
session = Session(bridge, "test")

for reply in bridge.dispatch(session, "GET https://example.com"):
    print(reply.encode().decode())
```

페이지를 구조체로만 받고 싶으면:

```python
from nova import web

page = web.fetch("https://example.com")
print(page.status, page.title)
for link in page.links:
    print(link.label, "->", link.url)
```

실제 소켓으로 확인:

```sh
python3 tools/ai_bridge.py &
printf 'GET https://example.com\n' | nc 127.0.0.1 7780
```

---

## 11. 아직 없는 것

`목표.md` 1단계에 해당하는 **`fs.*` / `proc.*` / `ui.*` 명령은 아직 없습니다.**

| 계획된 명령 | 상태 |
|---|---|
| `fs.list` / `fs.read` / `fs.write` | 미구현 — 커널 핸들러 필요 |
| `proc.list` / `proc.spawn` / `proc.kill` | 미구현 — 커널 핸들러 필요 |
| `ui.window.create` / `ui.window.draw` | 미구현 — 동적 UI 엔진 필요 |
| `boot.patch` | 미구현 — A/B 롤백 선행 필요 |

이것들은 **커널 쪽 핸들러가 함께 있어야** 동작하므로 파이썬만으로는 완결되지 않습니다.

반면 **호스트에서 완결되는 AI 함수는 지금 바로 추가**할 수 있습니다.
