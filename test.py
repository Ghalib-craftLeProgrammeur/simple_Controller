import socket
import threading
import time
import random

HOST = '127.0.0.1'
PORT = 4120

# ----------- Server Thread Function -----------
def start_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind((HOST, PORT))
    server_socket.listen(1)
    print(f"[Server] Listening on {HOST}:{PORT}...")

    conn, addr = server_socket.accept()
    print(f"[Server] Connected by {addr}")

    try:
        while True:
            data = conn.recv(1024)
            if not data:
                break
            packet = data.decode('utf-8')
            print(f"[Server] Received: {packet}")
    except Exception as e:
        print(f"[Server] Error: {e}")
    finally:
        conn.close()
        server_socket.close()
        print("[Server] Connection closed.")

# ----------- Client Thread Function -----------
def start_client():
    time.sleep(1)  # Delay to ensure server starts first

    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect((HOST, PORT))
    print(f"[Client] Connected to {HOST}:{PORT}")

    try:
        while True:
            orientationX = round(random.uniform(-180, 180), 2)
            orientationY = round(random.uniform(-180, 180), 2)
            orientationZ = round(random.uniform(-180, 180), 2)

            packet = f"{orientationX},{orientationY},{orientationZ}"
            client_socket.sendall(packet.encode('utf-8'))
            print(f"[Client] Sent: {packet}")
            time.sleep(0.5)
    except Exception as e:
        print(f"[Client] Error: {e}")
    finally:
        client_socket.close()
        print("[Client] Connection closed.")

# ----------- Run Threads -----------
server_thread = threading.Thread(target=start_server, daemon=True)
client_thread = threading.Thread(target=start_client, daemon=True)

server_thread.start()
client_thread.start()

# Keep the main thread alive
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("\n[Main] Program stopped.")
