#!/usr/bin/env python3
import json
import urllib.request
import time
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

# --- Configuration ---
GAS_URL = "https://script.google.com/macros/s/AKfycbyvnjYTnvkSDPWTnu60j87TtqKiTNB0CK3J9XgsJTPo_D94zdWuvd6XQS9fDg3ped1VQA/exec?key=MY_SECRET_KEY"
FETCH_INTERVAL = 3 * 60 * 60  # 3 hours in seconds
PORT = 8080  # Port 80 requires 'sudo', so 8080 is much safer/easier on Linux

# Global variable to hold the payload
e_ink_payload = json.dumps({"status": "Waiting for initial data..."})

def round_floats(obj):
    """Recursively walks through JSON data to round all floats to 1 decimal place."""
    if isinstance(obj, float):
        return round(obj, 1)
    elif isinstance(obj, dict):
        return {k: round_floats(v) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [round_floats(elem) for elem in obj]
    return obj

def fetch_google_data():
    """Fetches data from Google Apps Script on a set interval."""
    global e_ink_payload
    
    while True:
        print("\nFetching fresh data from Google Apps Script...")
        try:
            # urllib.request automatically follows the 302 redirects required by Google Apps Script
            req = urllib.request.Request(GAS_URL)
            with urllib.request.urlopen(req) as response:
                if response.status == 200:
                    # Read and parse the massive JSON string
                    data = json.loads(response.read().decode('utf-8'))
                    print("Data received. Processing and rounding floats to 1 dp...")
                    
                    # Clean up the floats before saving
                    cleaned_data = round_floats(data)
                    
                    # Serialize the modified JSON back into a string
                    e_ink_payload = json.dumps(cleaned_data)
                    print("Successfully updated payload!")
                else:
                    print(f"HTTP GET failed, status code: {response.status}")
        
        except Exception as e:
            print(f"Unable to connect or parse data: {e}")
            
        # Wait 3 hours before fetching again
        time.sleep(FETCH_INTERVAL)

class RequestHandler(BaseHTTPRequestHandler):
    """Handles incoming GET requests from the e-Ink display."""

    protocol_version = "HTTP/1.1"
    
    # Suppress default HTTP logging to keep the terminal clean
    def log_message(self, format, *args):
        pass

    def do_GET(self):
        print(f"Request received from {self.client_address[0]}")
        if self.path == '/':
            payload_bytes = e_ink_payload.encode('utf-8')
            print(f"Payload size: {len(payload_bytes)} bytes")
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Content-Length', str(len(payload_bytes)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(payload_bytes)
        else:
            self.send_response(404)
            self.end_headers()

def run_server():
    server_address = ('', PORT)
    httpd = HTTPServer(server_address, RequestHandler)
    
    print(f"Linux local network IP Address: (Check using 'ip a' in another terminal)")
    print(f"Server listening on http://localhost:{PORT}")
    print("Press Ctrl+C to stop.")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        httpd.server_close()

if __name__ == "__main__":
    # Start the Google fetcher in a background thread
    fetch_thread = threading.Thread(target=fetch_google_data, daemon=True)
    fetch_thread.start()
    
    # Start the HTTP server on the main thread
    run_server()