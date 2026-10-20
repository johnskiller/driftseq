#!/usr/bin/env python3
"""fw_check.py — host-side build + behavior check of the DriftSeq FM-1 app
layer. Compiles tools/fw_hostcheck.c (engine UNCHANGED + Felucca stubs) and
runs its 21 checks. Run before every firmware commit:
    python3 tools/fw_check.py
"""
import subprocess, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INC = [os.path.join(ROOT, d) for d in ('host', 'firmware', 'firmware/src')]
SRC = os.path.join(ROOT, 'tools', 'fw_hostcheck.c')
OUT = '/tmp/ds_fw_hostcheck'

r = subprocess.run(['gcc', '-Wall', '-O1', '-Werror=implicit-function-declaration',
                    '-DDS_WEB_UI=1']
                   + sum([['-I', d] for d in INC], [])
                   + [SRC, '-o', OUT], capture_output=True, text=True)
if r.returncode:
    print(r.stderr)
    sys.exit(1)
r = subprocess.run([OUT], capture_output=True, text=True, timeout=60)
print(r.stdout, end='')
sys.exit(r.returncode)
