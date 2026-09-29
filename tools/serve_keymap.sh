#!/usr/bin/env bash
# Serve keymap UI on LAN. Web Serial needs a secure context:
#   HTTPS (this script)  or  localhost  or  Chrome "insecure origin as secure"
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/web/keymap"
CERT_DIR="$DIR/.certs"
HOST="${HOST:-0.0.0.0}"
PORT="${PORT:-8766}"
MODE="${1:-https}" # https | http

mkdir -p "$CERT_DIR"
cd "$DIR"

if [[ "$MODE" == "http" ]]; then
  echo "HTTP on http://<本机局域网IP>:$PORT"
  echo "注意：局域网纯 HTTP 默认没有 Web Serial。"
  echo "开发机可用 Chrome 标记为安全源："
  echo "  chrome://flags/#unsafely-treat-insecure-origin-as-secure"
  echo "  填入 http://<IP>:$PORT 后重启浏览器"
  echo
  exec python3 -m http.server "$PORT" --bind "$HOST"
fi

KEY="$CERT_DIR/key.pem"
CRT="$CERT_DIR/cert.pem"
if [[ ! -f "$KEY" || ! -f "$CRT" ]]; then
  echo "生成自签证书 → $CERT_DIR"
  openssl req -x509 -newkey rsa:2048 -sha256 -days 825 -nodes \
    -keyout "$KEY" -out "$CRT" \
    -subj "/CN=Zhiyan/O=Zhiyan/C=CN" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" 2>/dev/null \
  || openssl req -x509 -newkey rsa:2048 -sha256 -days 825 -nodes \
    -keyout "$KEY" -out "$CRT" \
    -subj "/CN=Zhiyan"
fi

echo "HTTPS on https://<本机局域网IP>:$PORT （首次需在浏览器点「继续访问」）"
echo "本机也可：https://localhost:$PORT"
echo

exec python3 - "$HOST" "$PORT" "$KEY" "$CRT" <<'PY'
import http.server, ssl, sys
host, port, key, crt = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
httpd = http.server.HTTPServer((host, port), http.server.SimpleHTTPRequestHandler)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(certfile=crt, keyfile=key)
httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
print(f"Serving HTTPS on {host}:{port}", flush=True)
httpd.serve_forever()
PY
