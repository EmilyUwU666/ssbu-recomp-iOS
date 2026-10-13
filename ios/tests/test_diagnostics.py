#!/usr/bin/env python3
"""Verify fatal diagnostics only in host subprocesses; disable core files."""
import argparse
import resource
import signal
import subprocess


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('probe')
    args = parser.parse_args()
    for mode, expected in [('--trap-test', 'SSBU GUEST TRAP'),
                           ('--unimpl-test', 'SSBU UNTRANSLATED'),
                           ('--svc-test', 'unsupported direct kernel SVC')]:
        result = subprocess.run([args.probe, mode], capture_output=True, text=True,
                                timeout=10, preexec_fn=no_core)
        if result.returncode != -signal.SIGABRT or expected not in result.stderr:
            raise SystemExit(f'Fatal diagnostic failed: {mode}, exit {result.returncode}: {result.stderr}')
    print('PASS: trap, untranslated instruction and unsupported SVC terminate with diagnostics')


if __name__ == '__main__':
    main()
