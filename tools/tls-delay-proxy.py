#!/usr/bin/env python3
"""Tiny diagnostic TCP relay that delays server-to-client chunks."""

import socket
import threading
import time


# 诊断代理只监听本机，避免无认证转发器暴露到局域网/公网。
LISTEN = ("127.0.0.1", 8443)
UPSTREAM = ("127.0.0.1", 9443)
SERVER_DELAY_SECONDS = 0.5


def relay(source, target, delay_event=None):
    try:
        while True:
            data = source.recv(4096)
            if not data:
                break
            if delay_event is not None and delay_event.is_set():
                time.sleep(SERVER_DELAY_SECONDS)
            target.sendall(data)
    finally:
        try:
            target.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def handle(client):
    upstream = socket.create_connection(UPSTREAM)
    print(f"accepted {client.getpeername()}", flush=True)
    delay_server = threading.Event()

    def client_to_server():
        total = 0
        try:
            while True:
                data = client.recv(4096)
                if not data:
                    break
                total += len(data)
                upstream.sendall(data)
                if total > 262:
                    delay_server.set()
        finally:
            try:
                upstream.shutdown(socket.SHUT_WR)
            except OSError:
                pass

    left = threading.Thread(target=client_to_server, daemon=True)
    right = threading.Thread(
        target=relay,
        args=(upstream, client, delay_server),
        daemon=True,
    )
    left.start()
    right.start()
    left.join()
    right.join()
    client.close()
    upstream.close()


with socket.create_server(LISTEN, reuse_port=True) as listener:
    print(
        f"relay {LISTEN} -> {UPSTREAM}, post-ClientHello delay={SERVER_DELAY_SECONDS}s",
        flush=True,
    )
    while True:
        connection, _ = listener.accept()
        threading.Thread(target=handle, args=(connection,), daemon=True).start()
