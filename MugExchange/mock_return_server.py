from flask import Flask, send_from_directory, jsonify
from datetime import datetime
import os
import sys

app = Flask(__name__)

RETURN_TS = None
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

def log(msg):
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()

@app.before_request
def before():
    log("[SERVER] Incoming request")

@app.route("/ping")
def ping():
    log("[SERVER] /ping was called")
    return "pong", 200

@app.route("/")
def serve_app():
    path = os.path.join(BASE_DIR, "test.html")
    if os.path.exists(path):
        log("[SERVER] / -> serving test.html")
        return send_from_directory(BASE_DIR, "test.html")
    else:
        log("[SERVER] / -> test.html NOT FOUND")
        return "<h1>test.html not found</h1>", 404

@app.route("/mock-return", methods=["POST"])
def mock_return():
    global RETURN_TS
    RETURN_TS = datetime.utcnow().isoformat()
    log(f"[SERVER] /mock-return -> Mug marked returned at {RETURN_TS}")
    return jsonify({"ok": True, "ts": RETURN_TS})

@app.route("/mock-return-status", methods=["GET"])
def mock_return_status():
    log(f"[SERVER] /mock-return-status -> RETURN_TS={RETURN_TS}")
    return jsonify({"ts": RETURN_TS})

if __name__ == "__main__":
    log("Starting mock_return_server on http://127.0.0.1:8000 ...")
    app.run(host="127.0.0.1", port=8000, debug=False, use_reloader=False)
