#!/usr/bin/env python3
"""Receive the phone's sound from the S9Audio driver (UDP 5141) on the PC.

  s9-audio-recv.py OUT.wav [--seconds N] [--play]

Each datagram: 16-byte header (magic "S9AU", sequence, sample rate,
frames | channels << 16 | bits << 24, little endian) and 16-bit PCM.
Writes a WAV file; --play also pipes the sound to pw-play or aplay.
Prints, every 5 s and at the end, packets, frames and lost packets.
"""
import argparse
import shutil
import socket
import struct
import subprocess
import sys
import time
import wave


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--seconds', type=float, default=0, help='stop after N seconds (0 = until Ctrl-C)')
    ap.add_argument('--play', action='store_true')
    a = ap.parse_args()

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('0.0.0.0', 5141))
    s.settimeout(0.5)
    w = wave.open(a.out, 'wb')
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(48000)
    player = None
    if a.play:
        if shutil.which('pw-play'):
            cmd = ['pw-play', '--rate', '48000', '--channels', '2', '--format', 's16', '-']
        elif shutil.which('aplay'):
            cmd = ['aplay', '-q', '-f', 'S16_LE', '-r', '48000', '-c', '2']
        else:
            cmd = None
            print('no pw-play or aplay: recording only', file=sys.stderr)
        if cmd:
            player = subprocess.Popen(cmd, stdin=subprocess.PIPE)

    start = last = time.time()
    packets = frames = lost = 0
    expect = None
    try:
        while not a.seconds or time.time() - start < a.seconds:
            try:
                data, _ = s.recvfrom(65536)
            except socket.timeout:
                data = None
            if data and len(data) >= 16:
                magic, seq, rate, info = struct.unpack_from('<IIII', data)
                n = info & 0xffff
                if magic != 0x55413953 or len(data) != 16 + n * 4:
                    continue
                if expect is not None and seq != expect:
                    lost += (seq - expect) & 0xffffffff
                expect = (seq + 1) & 0xffffffff
                pcm = data[16:]
                w.writeframes(pcm)
                if player:
                    player.stdin.write(pcm)
                packets += 1
                frames += n
            if time.time() - last >= 5:
                last = time.time()
                print(f'{last - start:6.1f} s: {packets} packets, {frames} frames ({frames / 48000:.1f} s of sound), '
                      f'{lost} lost', flush=True)
    except KeyboardInterrupt:
        pass
    w.close()
    if player:
        player.stdin.close()
        player.wait()
    print(f'total: {packets} packets, {frames} frames ({frames / 48000:.2f} s of sound), {lost} lost -> {a.out}')


if __name__ == '__main__':
    main()
