#!/usr/bin/env bash
# Runs the AMWA NMOS Testing Tool suites that cover what the compositor's Node
# claims -- IS-04-01, IS-05-01, IS-05-02 and BCP-007-03-01 -- against it and a
# real nmos-cpp registry. Exits non-zero if any suite fails.
#
#   tests/nmos/run.sh              # builds the image, runs every suite
#   COMPOSITOR_IMAGE=... run.sh   # uses an already built image
#   run.sh BCP-007-03-01          # one suite
set -euo pipefail

cd "$(dirname "$0")"
REPO_ROOT=$(cd ../.. && pwd)
export COMPOSITOR_IMAGE="${COMPOSITOR_IMAGE:-compositor-nmos:dev}"
NODE=172.30.99.20
PORT=8080

if [ "${COMPOSITOR_IMAGE}" = "compositor-nmos:dev" ]; then
  docker build -t "${COMPOSITOR_IMAGE}" "${REPO_ROOT}"
fi

# The domain the platform would have materialised: a tmpfs directory with
# the domain_def.json BCP-007-03 has every domain carry.
DOMAIN_DIR=$(mktemp -d /dev/shm/compositor-nmos-lane.XXXXXX)
export DOMAIN_DIR
cat >"${DOMAIN_DIR}/domain_def.json" <<'JSON'
{"id":"6f1c2a3b-4d5e-4f60-8a71-b2c3d4e5f607","label":"lane","description":"conformance lane domain","tags":{}}
JSON
chmod 0755 "${DOMAIN_DIR}"

cleanup() {
  docker compose down -v --remove-orphans >/dev/null 2>&1 || true
  rm -rf "${DOMAIN_DIR}"
}
trap cleanup EXIT
docker compose down -v --remove-orphans >/dev/null 2>&1 || true
docker compose up -d registry rtsp compositor

# The Node registers once it has read the domain's identity; the suites need
# it registered, so wait for the registry to list it.
for _ in $(seq 1 60); do
  if docker compose exec -T registry sh -c \
      "curl -s http://127.0.0.1:8010/x-nmos/query/v1.3/receivers/" 2>/dev/null \
      | grep -q '"transport"'; then
    break
  fi
  sleep 2
done

suites=("$@")
[ ${#suites[@]} -gt 0 ] || suites=(IS-04-01 IS-05-01 IS-05-02 BCP-007-03-01)

rc=0
for suite in "${suites[@]}"; do
  case "$suite" in
    IS-04-01)      args=(--host "$NODE" --port "$PORT" --version v1.3) ;;
    IS-05-01)      args=(--host "$NODE" --port "$PORT" --version v1.2) ;;
    IS-05-02)      args=(--host "$NODE" "$NODE" --port "$PORT" "$PORT" --version v1.3 v1.2) ;;
    BCP-007-03-01) args=(--host "$NODE" "$NODE" --port "$PORT" "$PORT" --version v1.3 v1.2) ;;
    *) echo "unknown suite $suite" >&2; exit 2 ;;
  esac
  log="$(mktemp)"
  docker compose run --rm testing suite "$suite" "${args[@]}" >"$log" 2>&1 || true
  junit=$(sed -n '/----- BEGIN JUNIT -----/,/----- END JUNIT -----/p' "$log")
  # The tool exits non-zero on warnings too. A warning is advice -- the
  # deprecated IS-04 senders/receivers attributes, for one -- so only a
  # failure of type Fail, an error, or no report at all fails the suite.
  if ! grep -q '<testsuite' <<<"$junit"; then
    echo "FAIL $suite (no report written)"
    tail -20 "$log" | sed 's/^/    /'
    rc=1
  elif grep -qE '<failure type="Fail"|<error ' <<<"$junit"; then
    echo "FAIL $suite"
    rc=1
  else
    echo "PASS $suite"
  fi
  grep -oE '<(failure|error) [^>]*message="[^"]*' <<<"$junit" | sed 's/^/    /' || true
  rm -f "$log"
done
exit $rc
