#!/usr/bin/env python3
# `pebble send-app-message` fires the AppMessage packet and tears the connection
# down immediately (pebble_tool/commands/appmessage.py) without waiting for the
# watch's ACK — libpebble2 exposes ack/nack events but the CLI never listens for
# them. That's a real gap in the tool: messages get silently dropped whenever the
# watch hasn't finished processing before the connection closes. This waits for
# ack/nack before exiting, so a non-zero exit code actually means something.
#
# Run with the pebble-tool venv's own interpreter (it has libpebble2 installed):
#   ~/.local/share/uv/tools/pebble-tool/bin/python3 send_tide_message.py <uuid> i:10002=120 s:10004=abcd

import os
import sys
import threading
import uuid as uuid_module

from libpebble2.communication import PebbleConnection
from libpebble2.services.appmessage import AppMessageService, Int32, CString
from pebble_tool.sdk.emulator import ManagedEmulatorTransport

ACK_TIMEOUT_S = 8


def parse_fields(entries):
    dictionary = {}
    for entry in entries:
        try:
            kind, rest = entry.split(':', 1)
            key_str, value = rest.split('=', 1)
            key = int(key_str)
        except ValueError:
            raise SystemExit("Bad field '{}', expected TYPE:KEY=VALUE (TYPE is i or s)".format(entry))
        if kind == 'i':
            dictionary[key] = Int32(int(value))
        elif kind == 's':
            dictionary[key] = CString(value)
        else:
            raise SystemExit("Unknown field type '{}' in '{}' (use i or s)".format(kind, entry))
    return dictionary


def main():
    if len(sys.argv) < 3:
        print("usage: send_tide_message.py <app-uuid> <TYPE:KEY=VALUE> ...", file=sys.stderr)
        sys.exit(2)

    target = uuid_module.UUID(sys.argv[1])
    dictionary = parse_fields(sys.argv[2:])

    platform = os.environ.get('PEBBLE_EMULATOR_PLATFORM', 'emery')
    transport = ManagedEmulatorTransport(platform, None, False)
    connection = PebbleConnection(transport)
    connection.connect()
    connection.run_async()

    service = AppMessageService(connection)
    done = threading.Event()
    outcome = {}

    def on_ack(transaction_id, app_uuid):
        outcome['result'] = 'ack'
        done.set()

    def on_nack(transaction_id, app_uuid):
        outcome['result'] = 'nack'
        done.set()

    service.register_handler('ack', on_ack)
    service.register_handler('nack', on_nack)
    service.send_message(target, dictionary)

    got_response = done.wait(timeout=ACK_TIMEOUT_S)
    service.shutdown()

    if not got_response:
        print("Timed out waiting for ACK — watch likely didn't receive it", file=sys.stderr)
        sys.exit(1)
    if outcome.get('result') != 'ack':
        print("Watch NACKed the message", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
