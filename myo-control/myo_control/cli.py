"""Command-line entry point: `python -m myo_control <subcommand>`."""

from __future__ import annotations

import argparse


def main() -> None:
    parser = argparse.ArgumentParser(prog="myo_control")
    sub = parser.add_subparsers(dest="command", required=True)

    p_ports = sub.add_parser("list-ports", help="List serial ports (find the Myo dongle's COM port)")

    p_record = sub.add_parser("record", help="Record a training session")
    p_record.add_argument("--name", default=None, help="Session name (default: timestamp)")
    p_record.add_argument("--port", default=None, help="Dongle COM port (default: auto-detect)")

    p_train = sub.add_parser("train", help="Train gesture + cursor models from recorded sessions")
    p_train.add_argument("--sessions", nargs="*", default=None, help="Session names (default: all)")

    p_control = sub.add_parser("control", help="Run the live control loop")
    p_control.add_argument("--port", default=None, help="Dongle COM port (default: auto-detect)")

    args = parser.parse_args()

    if args.command == "list-ports":
        from .dongle import list_candidate_ports

        ports = list_candidate_ports()
        if not ports:
            print("No serial ports found.")
        for p in ports:
            print(p)
    elif args.command == "record":
        from .record import run as record_run

        record_run(args.name, args.port)
    elif args.command == "train":
        from .train import run as train_run

        train_run(args.sessions)
    elif args.command == "control":
        from .control import run as control_run

        control_run(args.port)


if __name__ == "__main__":
    main()
