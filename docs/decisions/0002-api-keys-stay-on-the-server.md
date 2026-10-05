# 0002: API keys stay on the server; browser pages talk to a local server

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: owner of api-data-fetching

## Context
We want simple web pages that show live IMS data, for example product cards that update by themselves. The IMS API needs a key sent as a bearer token. A page that holds the key exposes it to anyone who opens the file or its source, and to browser history and caches. IMS offers no push, webhook or streaming feed, and allows 120 requests per minute per key, so many pages calling it directly would burn the limit.

## Decision
1. A key is only ever read by a server-side process, from environment variables in the local `.env`. It is never written into HTML, JavaScript sent to a browser, a URL, a log or a document.
2. A browser page talks only to a small local server. That server calls IMS, polls it once on a timer, detects changes, and pushes them to open pages with server-sent events.
3. The local server listens on `127.0.0.1` only, refuses requests with a foreign `Host` header, serves only GET, and sets a restrictive Content Security Policy. Product text is rendered as plain text.
4. Anything that must be reachable by other people over a network needs real authentication added first.

Reference implementation: [live-product-cards](../../api-data-fetching/tasks/2026-10-05-live-product-cards/README.md).

## Alternatives considered
- **Key inside the HTML file**: simplest, but leaks the key. Rejected.
- **Key in the page URL (`?api_key=`)**: the API allows this for read-only keys, but the key then lands in server logs, history and the Referer header. Rejected, and deliberately not tested.
- **Each browser polls IMS directly**: needs the key in the browser, and every open page spends the shared rate limit. Rejected.
- **WebSockets or webhooks**: IMS documents neither.

## Consequences
- A page only works while its server process runs. A stopped server shows "localhost refused to connect" (see [troubleshooting](../../api-data-fetching/troubleshooting/localhost-refused-to-connect.md)).
- Updates can lag a change in IMS by up to one poll interval (5 seconds by default).
- Revisit if IMS adds a push or streaming feed, or if a shared hosted version is needed.
