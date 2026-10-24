#!/usr/bin/env python3
"""Run a Frida JS snippet inside a process on the phone's iOS, with ObjC.

  s9-objc.py PROCESS SCRIPT.js [--wait SECONDS]
  s9-objc.py PROCESS -e 'console.log(ObjC.classes.UIScreen.mainScreen().scale())'

Needs frida-server on the phone listening on the USB network (installed from
the frida iphoneos-arm .deb; the stock launchd job only listens on
localhost, so tools/s9-frida-up.sh starts it with -l 0.0.0.0), the Python
frida module of the same version on the PC and `npm install` in tools/frida
(Frida 17 no longer bundles the ObjC bridge: the script is compiled with it).
The script sees ObjC as a global; console.log lines are printed here.
"""
import argparse, os, sys, tempfile, time
import frida

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.join(HERE, 'frida')
HOST = os.environ.get('S9_HOST', '192.168.7.2')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('process')
    ap.add_argument('script', nargs='?')
    ap.add_argument('-e', dest='expr')
    ap.add_argument('--wait', type=float, default=2.0, help='seconds to keep the script alive after load')
    a = ap.parse_args()
    body = a.expr if a.expr is not None else open(a.script).read()
    src = 'import ObjC from "frida-objc-bridge";\nglobalThis.ObjC = ObjC;\n' + body
    with tempfile.NamedTemporaryFile('w', suffix='.js', dir=PROJ, delete=False) as f:
        f.write(src)
        path = f.name
    try:
        # terser: the USB link drops frida messages over ~128 KiB; the bridge alone is 149 KiB
        bundle = frida.Compiler().build(os.path.basename(path), project_root=PROJ, compression='terser')
    finally:
        os.unlink(path)
    dev = frida.get_device_manager().add_remote_device(HOST)
    target = int(a.process) if a.process.isdigit() else a.process
    sess = dev.attach(target)
    sc = sess.create_script(bundle)
    done = []

    def on_msg(m, data):
        if m['type'] == 'send':
            if m['payload'] == '__done__':
                done.append(1)
            else:
                print(m['payload'], flush=True)
        else:
            print(m.get('stack') or m, file=sys.stderr, flush=True)
    sc.on('message', on_msg)
    sc.load()
    end = time.time() + a.wait
    while time.time() < end and not done:
        time.sleep(0.05)
    sess.detach()


if __name__ == '__main__':
    main()
