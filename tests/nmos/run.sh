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

# The runtime root the platform would have materialised, on tmpfs: the
# primary domain, and a second one at domains/<id>, each with the
# domain_def.json BCP-007-03 has every domain carry.
PRIMARY_ID=6f1c2a3b-4d5e-4f60-8a71-b2c3d4e5f607
SECOND_ID=fec11c1b-9fab-4275-ab2c-ef676fa2e081
MXL_ROOT=$(mktemp -d /dev/shm/compositor-nmos-lane.XXXXXX)
export MXL_ROOT
mkdir -p "${MXL_ROOT}/domain" "${MXL_ROOT}/domains/${SECOND_ID}"
for d in "domain:${PRIMARY_ID}" "domains/${SECOND_ID}:${SECOND_ID}"; do
  printf '{"id":"%s","label":"lane","description":"conformance lane domain","tags":{}}\n' \
    "${d#*:}" >"${MXL_ROOT}/${d%%:*}/domain_def.json"
done
chmod -R 0755 "${MXL_ROOT}"

cleanup() {
  docker compose down -v --remove-orphans >/dev/null 2>&1 || true
  rm -rf "${MXL_ROOT}"
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

# Every Connection API href the Node advertises has to be reachable at the
# address it was given. A controller tries the hrefs in order, and a pod's own
# host name does not resolve anywhere outside it -- a controller that stops at
# the first unresolvable one cannot connect anything.
hrefs=$(docker compose exec -T registry sh -c \
    "curl -s http://127.0.0.1:8010/x-nmos/query/v1.3/devices/" 2>/dev/null \
  | grep -oE '"href":"http://[^"]*/x-nmos/connection/[^"]*"' | sort -u)
if [ -z "$hrefs" ] || grep -v "http://$NODE:" <<<"$hrefs" >/dev/null; then
  echo "FAIL advertised hrefs"
  sed 's/^/    /' <<<"${hrefs:-(none)}"
  hrefs_rc=1
else
  echo "PASS advertised hrefs"
  hrefs_rc=0
fi

# Every tile can be connected in either domain, and in no other: its
# mxl_domain_id constraint lists both, and a PATCH naming another is refused
# by IS-05 before the compositor sees it.
conn="http://$NODE:$PORT/x-nmos/connection/v1.2/single/receivers"
in_lane() { docker compose exec -T registry curl -s "$@"; }
rxs=$(in_lane "$conn/" | grep -oE '[0-9a-f-]{36}' | sort -u)
domains_rc=0
[ "$(wc -w <<<"$rxs")" -eq 4 ] || { echo "FAIL tile count: $(wc -w <<<"$rxs") receivers"; domains_rc=1; }
for rx in $rxs; do
  c=$(in_lane "$conn/$rx/constraints")
  grep -q "$PRIMARY_ID" <<<"$c" && grep -q "$SECOND_ID" <<<"$c" || {
    echo "FAIL $rx constraints do not list both domains: $c"; domains_rc=1; }
done
rx=$(head -1 <<<"$rxs")
patch() {
  in_lane -o /dev/null -w '%{http_code}' -X PATCH -H 'Content-Type: application/json' \
    "$conn/$rx/staged" -d "{\"master_enable\":true,\"activation\":{\"mode\":\"activate_immediate\"},\"transport_params\":[{\"mxl_flow_id\":\"11111111-2222-4333-8444-555555555555\",\"mxl_domain_id\":\"$1\"}]}"
}
[ "$(patch "$SECOND_ID")" = 200 ] || { echo "FAIL a tile could not be connected in the second domain"; domains_rc=1; }
sleep 1
docker compose logs compositor 2>/dev/null | grep -q "in /run/mxl/domains/${SECOND_ID}" || {
  echo "FAIL the compositor did not read the second domain's directory"; domains_rc=1; }
[ "$(patch 0a000000-0000-4000-8000-000000000001)" = 400 ] || {
  echo "FAIL a domain the compositor does not read was accepted"; domains_rc=1; }
[ $domains_rc -eq 0 ] && echo "PASS domains"

suites=("$@")
[ ${#suites[@]} -gt 0 ] || suites=(IS-04-01 IS-05-01 IS-05-02 BCP-007-03-01)

rc=$((hrefs_rc | domains_rc))
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
