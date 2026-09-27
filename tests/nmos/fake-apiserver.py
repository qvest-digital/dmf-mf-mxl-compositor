"""The part of the Kubernetes API the compositor keeps its connections in: one
ConfigMap, read with GET and written with a JSON merge patch, over TLS and
behind a bearer token, as the API server serves it to a pod. The ConfigMap
exists from the start, as the chart creates it, and lives as long as this
process, so it outlives a restart of the compositor.
"""

import json
import ssl
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

CERT, KEY, TOKEN, PATH = sys.argv[1:5]
configmap = {"kind": "ConfigMap", "apiVersion": "v1",
             "metadata": {"name": PATH.rsplit("/", 1)[1]}, "data": {}}


class Handler(BaseHTTPRequestHandler):
    def reply(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def allowed(self):
        if self.headers.get("Authorization") != "Bearer " + TOKEN:
            self.reply(401, {"kind": "Status", "code": 401})
            return False
        if self.path != PATH:
            self.reply(404, {"kind": "Status", "code": 404})
            return False
        return True

    def do_GET(self):
        if self.allowed():
            self.reply(200, configmap)

    def do_PATCH(self):
        if not self.allowed():
            return
        if self.headers.get("Content-Type") != "application/merge-patch+json":
            self.reply(415, {"kind": "Status", "code": 415})
            return
        patch = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        for key, value in patch.get("data", {}).items():
            if value is None:
                configmap["data"].pop(key, None)
            else:
                configmap["data"][key] = value
        self.reply(200, configmap)


server = HTTPServer(("0.0.0.0", 6443), Handler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(CERT, KEY)
server.socket = context.wrap_socket(server.socket, server_side=True)
server.serve_forever()
