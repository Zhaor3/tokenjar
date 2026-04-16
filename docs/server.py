import http.server, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
http.server.HTTPServer(("127.0.0.1", 8091), http.server.SimpleHTTPRequestHandler).serve_forever()
