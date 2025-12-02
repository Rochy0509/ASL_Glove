import argparse
import csv
import re
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, TextIO, Tuple

import serial
import serial.tools.list_ports
from serial.tools import list_ports_common

DATA_HEADER = [
    "person_id",
    "label",
    "timestamp",
    "flex1",
    "flex2",
    "flex3",
    "flex4",
    "flex5",
    "ax",
    "ay",
    "az",
    "gx",
    "gy",
    "gz",
]

LABEL_OPTIONS = [chr(value) for value in range(ord("A"), ord("Z") + 1)] + [
    "NEUTRAL",
    "SPACE",
    "BACKSPACE",
]

DEFAULT_BAUD = 115200
DEFAULT_OUTPUT_DIR = Path(__file__).resolve().parents[1] / "data_logs"


def list_serial_ports() -> List[list_ports_common.ListPortInfo]:
    return list(serial.tools.list_ports.comports())


def prompt_serial_port() -> str:
    ports = list_serial_ports()
    if not ports:
        print("No serial ports detected. Connect the ESP32-S3 and try again.")
        sys.exit(1)

    print("\nAvailable serial ports:")
    for idx, port in enumerate(ports, start=1):
        print(f"  {idx}) {port.device} - {port.description}")
    print("  0) Enter a custom port path\n")

    while True:
        choice = input("Select port [1]: ").strip()
        if not choice:
            return ports[0].device
        if choice == "0":
            manual = input("Enter the serial port path (e.g., /dev/ttyUSB0 or COM5): ").strip()
            if manual:
                return manual
        elif choice.isdigit():
            index = int(choice) - 1
            if 0 <= index < len(ports):
                return ports[index].device

        print("Invalid selection. Try again.")


def prompt_person_id(default: str = "P1") -> str:
    while True:
        value = input(f"Enter person ID (e.g., P1, P2) [{default}]: ").strip()
        if not value:
            return default.upper()
        if re.match(r"^[A-Za-z0-9_-]+$", value):
            return value.upper()
        print("Use letters, numbers, '-' or '_' only.")


def prompt_label(default: str = "A") -> str:
    print("\nSelect label to record:")
    for idx, option in enumerate(LABEL_OPTIONS, start=1):
        print(f"  {idx:2}) {option}")
    custom_index = len(LABEL_OPTIONS) + 1
    print(f"  {custom_index:2}) Custom label")

    while True:
        selection = input(f"Choose label [default {default}]: ").strip()
        if not selection:
            return default.upper()
        if selection.isdigit():
            idx = int(selection)
            if 1 <= idx <= len(LABEL_OPTIONS):
                return LABEL_OPTIONS[idx - 1]
            if idx == custom_index:
                custom = input("Enter custom label: ").strip()
                if custom:
                    return custom.upper()
        else:
            return selection.upper()
        print("Invalid selection. Try again.")


def prompt_yes_no(prompt: str, default: bool = False) -> bool:
    default_text = "Y/n" if default else "y/N"
    question = f"{prompt} [{default_text}]: "
    while True:
        reply = input(question).strip().lower()
        if not reply:
            return default
        if reply in ("y", "yes"):
            return True
        if reply in ("n", "no"):
            return False
        print("Please respond with 'y' or 'n'.")


def sanitize_person_id(person_id: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_-]", "_", person_id.strip().upper())
    return sanitized or "UNKNOWN"


def parse_sample_line(line: str) -> Optional[List[str]]:
    if "," not in line:
        return None
    parts = [piece.strip() for piece in line.split(",")]
    if len(parts) != len(DATA_HEADER):
        return None
    if not parts[0] or not parts[1]:
        return None
    try:
        int(parts[2])
        for value in parts[3:]:
            float(value)
    except ValueError:
        return None
    return parts


class CsvLogManager:
    def __init__(self, output_dir: Path) -> None:
        self.output_dir = output_dir
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self._open_files: Dict[str, Tuple[TextIO, csv.writer]] = {}

    def write_row(self, person_id: str, row: List[str]) -> None:
        key = sanitize_person_id(person_id)
        if key not in self._open_files:
            file_path = self.output_dir / f"{key}_data.csv"
            file_exists = file_path.exists()
            handle = open(file_path, "a", newline="")
            writer = csv.writer(handle)
            if not file_exists:
                writer.writerow(DATA_HEADER)
            self._open_files[key] = (handle, writer)
            print(f"[LOG] Now writing samples to {file_path}")

        handle, writer = self._open_files[key]
        writer.writerow(row)
        handle.flush()

    def close(self) -> None:
        for handle, _ in self._open_files.values():
            handle.close()
        self._open_files.clear()


def send_command(ser: serial.Serial, command: str, value: Optional[str] = None) -> None:
    if not command:
        return
    ser.write(command.encode("ascii"))
    time.sleep(0.1)
    if value is not None:
        ser.write(value.encode("ascii"))
        ser.write(b"\n")
    time.sleep(0.1)


def configure_device(
    ser: serial.Serial,
    person_id: str,
    label: str,
    auto_start: bool = True,
) -> None:
    person_id = person_id.upper()
    label = label.upper()
    print(f"[CMD] Setting person ID to {person_id}")
    send_command(ser, "p", person_id)
    print(f"[CMD] Setting label to {label}")
    send_command(ser, "l", label)
    if auto_start:
        print("[CMD] Arming logger (g)")
        send_command(ser, "g")
    else:
        print("[CMD] Logger armed manually. Use the PC prompt or type 'g' in a serial console when ready.")


def run_calibration(ser: serial.Serial) -> None:
    print("\n[CMD] Initiating flex calibration routine (r)")
    ser.reset_input_buffer()
    send_command(ser, "r")
    print("[INFO] Follow the prompts below. Use ENTER to respond when asked to press any key.\n")
    try:
        _drive_calibration_dialog(ser)
        print("[INFO] Calibration sequence completed.\n")
    except KeyboardInterrupt:
        print("\n[WARN] Calibration interrupted by user. Continuing without recalibration.\n")
    except RuntimeError as exc:
        print(f"[ERROR] {exc}\n")


def prompt_start_logging(ser: serial.Serial) -> None:
    input(
        "\nPress ENTER when you're ready to start logging (this sends the 'g' command to the glove)."
    )
    print("[CMD] Arming logger (g)")
    send_command(ser, "g")


def _drive_calibration_dialog(ser: serial.Serial) -> None:
    """
    Proxy serial output during calibration so the operator can press ENTER
    when the firmware requests input.
    """
    open_prompt_seen = False
    fist_prompt_seen = False

    while True:
        line_bytes = ser.readline()
        if not line_bytes:
            continue
        line = line_bytes.decode("utf-8", errors="ignore").strip()
        if not line:
            continue

        print(f"[ESP32] {line}")
        lower = line.lower()
        if "press any key to start" in lower and not open_prompt_seen:
            open_prompt_seen = True
            input("  → Open your hand fully, then press ENTER to begin sampling.")
            ser.write(b"\n")
        elif "press any key to continue" in lower and not fist_prompt_seen:
            fist_prompt_seen = True
            input("  → Make a tight fist, then press ENTER to sample the closed fist.")
            ser.write(b"\n")
        elif "calibration complete" in lower:
            return
        elif "error" in lower:
            raise RuntimeError("Firmware reported a calibration error.")


def log_serial_stream(
    ser: serial.Serial,
    log_manager: CsvLogManager,
    show_raw: bool = False,
) -> None:
    print("\nListening for sensor data... Press Ctrl+C to stop.\n")
    while True:
        try:
            line_bytes = ser.readline()
        except serial.SerialException as exc:
            print(f"\n[ERROR] Serial read failed: {exc}")
            break

        if not line_bytes:
            continue

        line = line_bytes.decode("utf-8", errors="ignore").strip()
        if not line:
            continue

        lower_line = line.lower()
        if lower_line.startswith("person_id"):
            print(f"[ESP32] {line}")
            continue

        row = parse_sample_line(line)
        if row:
            log_manager.write_row(row[0], row)
            if show_raw:
                print(f"[DATA] {line}")
            continue

        # Non-data lines are informational status updates from the firmware.
        print(f"[ESP32] {line}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Log ASL glove samples streamed from the ESP32-S3 into per-person CSV files.",
    )
    parser.add_argument("--port", help="Serial port (e.g., /dev/ttyUSB0, COM5)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    parser.add_argument(
        "--person",
        help="Person identifier (P1, P2, etc.). Prompted interactively when omitted.",
    )
    parser.add_argument(
        "--label",
        help="Label/letter to log (A-Z, NEUTRAL, etc.). Prompted interactively when omitted.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Directory for CSV logs (default: %(default)s)",
    )
    parser.add_argument(
        "--no-config",
        action="store_true",
        help="Skip sending configuration commands to the firmware – just log incoming data.",
    )
    parser.add_argument(
        "--calibrate",
        action="store_true",
        help="Send the calibration command ('r') before setting labels/person ID.",
    )
    parser.add_argument(
        "--auto-start",
        action="store_true",
        help="Automatically send 'g' after configuring person/label (skip the start prompt).",
    )
    parser.add_argument(
        "--no-autostart",
        action="store_true",
        help=argparse.SUPPRESS,  # Legacy flag – manual start is the default behavior now.
    )
    parser.add_argument(
        "--show-raw",
        action="store_true",
        help="Echo each parsed data row to the console.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    port = args.port or prompt_serial_port()

    display_rows = args.show_raw
    if not display_rows and sys.stdin.isatty():
        display_rows = prompt_yes_no(
            "Display each logged sample in the terminal while recording?", default=True
        )

    if not args.no_config:
        person_id = args.person or prompt_person_id()
        label = args.label or prompt_label()
        if args.calibrate:
            perform_calibration = True
        elif sys.stdin.isatty():
            perform_calibration = prompt_yes_no(
                "Run flex calibration before logging?", default=False
            )
        else:
            perform_calibration = False
    else:
        person_id = args.person or ""
        label = args.label or ""
        perform_calibration = False

    output_dir = Path(args.output_dir).expanduser()
    print(f"\nOpening serial port {port} at {args.baud} baud...")

    try:
        ser = serial.Serial(port, args.baud, timeout=1)
    except serial.SerialException as exc:
        print(f"Failed to open serial port {port}: {exc}")
        available = list_serial_ports()
        if available:
            print("\nDetected serial ports:")
            for item in available:
                print(f"  - {item.device}: {item.description}")
        return 1

    log_manager = CsvLogManager(output_dir)

    try:
        time.sleep(0.5)
        ser.reset_input_buffer()
        if not args.no_config:
            if perform_calibration:
                run_calibration(ser)
            configure_device(ser, person_id, label, auto_start=False)
            if args.auto_start:
                print("[CMD] Arming logger (g)")
                send_command(ser, "g")
            else:
                prompt_start_logging(ser)
        else:
            print("[INFO] Skipping firmware configuration; listening only.")

        log_serial_stream(ser, log_manager, show_raw=display_rows)
    except KeyboardInterrupt:
        print("\nStopping data capture ...")
    finally:
        log_manager.close()
        try:
            if ser.is_open and not args.no_config:
                print("[CMD] Sending stop command (t)")
                send_command(ser, "t")
        except serial.SerialException:
            pass
        if ser.is_open:
            ser.close()

    print(f"Logs saved in {output_dir}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
