#!/usr/bin/env python3
"""
PS3 CD-DA to LDAC/PCM High-Fidelity Audio Receiver
-------------------------------------------------
Accepts raw 16-bit 44.1kHz Stereo Little-Endian PCM over UDP,
buffers it via a low-latency ring buffer, and outputs to default soundcard.
"""

import sys
import socket
import argparse
import threading
import collections
import time

try:
    import sounddevice as sd
    import numpy as np
except ImportError:
    print("[!] Missing dependencies. Install them via:")
    print("    pip install sounddevice numpy")
    sys.exit(1)

SAMPLE_RATE = 44100
CHANNELS = 2
BYTES_PER_SAMPLE = 2  # 16-bit signed
FRAME_SIZE = CHANNELS * BYTES_PER_SAMPLE  # 4 bytes per stereo sample
DEFAULT_PORT = 12345
DEFAULT_BUFFER_MS = 80


class AudioJitterBuffer:
    def __init__(self, buffer_ms=80, block_frames=512):
        self.target_frames = int(SAMPLE_RATE * (buffer_ms / 1000.0))
        self.block_frames = block_frames
        self.buffer = bytearray()
        self.lock = threading.Lock()
        self.is_prebuffering = True
        self.underruns = 0
        self.overruns = 0

    def push(self, data: bytes):
        with self.lock:
            # Предотвращение неконтролируемого роста буфера (макс. 400 мс)
            max_bytes = self.target_frames * FRAME_SIZE * 5
            if len(self.buffer) > max_bytes:
                # Сброс старых данных для сохранения низкой задержки
                del self.buffer[: len(self.buffer) - (self.target_frames * FRAME_SIZE)]
                self.overruns += 1

            self.buffer.extend(data)

            if self.is_prebuffering:
                if len(self.buffer) >= (self.target_frames * FRAME_SIZE):
                    self.is_prebuffering = False

    def pull(self, frame_count: int) -> bytes:
        requested_bytes = frame_count * FRAME_SIZE
        with self.lock:
            if self.is_prebuffering:
                return b'\x00' * requested_bytes

            if len(self.buffer) < requested_bytes:
                # Опустошение буфера (Underflow): выдаем тишину и возвращаемся в пребуферизацию
                available = bytes(self.buffer)
                self.buffer.clear()
                self.is_prebuffering = True
                self.underruns += 1
                return available + (b'\x00' * (requested_bytes - len(available)))

            out_data = bytes(self.buffer[:requested_bytes])
            del self.buffer[:requested_bytes]
            return out_data

    def get_stats(self):
        with self.lock:
            current_ms = (len(self.buffer) / FRAME_SIZE / SAMPLE_RATE) * 1000.0
            return current_ms, self.underruns, self.overruns


def main():
    parser = argparse.ArgumentParser(description="PS3 CD-DA High-Quality UDP Receiver")
    parser.add_argument("--ip", default="0.0.0.0", help="Binding IP address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="UDP port (default: 12345)")
    parser.add_argument("--buffer", type=int, default=DEFAULT_BUFFER_MS, help="Jitter buffer in ms (default: 80)")
    parser.add_argument("--device", type=int, default=None, help="Audio output device index")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)  # 1MB socket buffer
    sock.bind((args.ip, args.port))

    jitter_buffer = AudioJitterBuffer(buffer_ms=args.buffer)
    running = True

    def audio_callback(outdata, frames, time_info, status):
        raw_bytes = jitter_buffer.pull(frames)
        # Преобразование Little-Endian 16-bit PCM в массив numpy
        samples = np.frombuffer(raw_bytes, dtype=np.int16)
        outdata[:] = samples.reshape((frames, CHANNELS))

    def network_worker():
        while running:
            try:
                data, _ = sock.recvfrom(2048)
                if data:
                    jitter_buffer.push(data)
            except Exception:
                break

    net_thread = threading.Thread(target=network_worker, daemon=True)
    net_thread.start()

    print("=====================================================")
    print(f" PS3 CD-DA Audio Stream Receiver listening on {args.ip}:{args.port}")
    print(f" Jitter Buffer Target: {args.buffer} ms | Format: 44.1 kHz, 16-bit Stereo LE")
    print(" Ready to stream to Bluetooth LDAC / DAC")
    print(" Press Ctrl+C to stop.")
    print("=====================================================")

    try:
        with sd.OutputStream(samplerate=SAMPLE_RATE,
                             channels=CHANNELS,
                             dtype='int16',
                             blocksize=256,
                             device=args.device,
                             callback=audio_callback):
            while True:
                time.sleep(1.0)
                cur_ms, underruns, overruns = jitter_buffer.get_stats()
                sys.stdout.write(f"\r[Status] Buffer: {cur_ms:5.1f} ms | Underruns: {underruns} | Overruns: {overruns}")
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\nStopping receiver...")
    finally:
        running = False
        sock.close()


if __name__ == "__main__":
    main()
