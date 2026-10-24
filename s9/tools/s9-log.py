#!/usr/bin/env python3
"""Live kernel console of the phone's iOS over the USB cable.

  s9-log.py LOGFILE

The kernel (sweet_usb.c, boot-arg sweetusblog, on by default) sends the new
bytes of its console ring every 100 ms as UDP datagrams to 192.168.7.1:5140,
starting with the last 64 KiB. This appends them to LOGFILE, flushed per
datagram, until killed. The PC side address comes from the NetworkManager
profile s9-usb (tools/prep-ios6.sh).
"""
import socket
import sys


def main():
    out = open(sys.argv[1], 'ab', buffering=0)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('0.0.0.0', 5140))
    while True:
        data, _ = s.recvfrom(65536)
        out.write(data)


if __name__ == '__main__':
    main()
