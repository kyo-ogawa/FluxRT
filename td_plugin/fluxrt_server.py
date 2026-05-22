"""FluxRT inference server for TouchDesigner C++ TOP.

Protocol:
  stdout line 1: FLUXRT_INPUT=<shm_name>
  stdout line 2: FLUXRT_OUTPUT=<shm_name>
  stdout line 3: FLUXRT_CTRL=<shm_name>
  stdout line 4: FLUXRT_READY
  (then runs until ctrl.shutdown_flag is set)
"""
import argparse
import struct
import sys
import time
from multiprocessing import shared_memory

import numpy as np

# ── Status codes (must match FluxRTCtrl.h) ────────────────────────────────
STATUS_LOADING    = 0
STATUS_COMPILING  = 1
STATUS_RUNNING    = 2
STATUS_ERROR      = 3

CTRL_SIZE = 4096


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--width",  type=int, required=True)
    p.add_argument("--height", type=int, required=True)
    p.add_argument("--int8",   action="store_true")
    p.add_argument("--config", default="configs/config_with_reference.json")
    return p.parse_args()


class CtrlBlock:
    """Read/write access to the fluxrt_ctrl shared memory block."""

    def __init__(self, shm: shared_memory.SharedMemory):
        self._buf = shm.buf

    # ── read properties ────────────────────────────────────────────────────

    @property
    def prompt(self) -> str:
        raw = bytes(self._buf[0:1024])
        return raw.split(b"\x00")[0].decode("utf-8", errors="replace")

    @property
    def reference_image_path(self) -> str:
        raw = bytes(self._buf[1024:1536])
        return raw.split(b"\x00")[0].decode("utf-8", errors="replace")

    @property
    def steps(self) -> int:
        return struct.unpack_from("<i", self._buf, 1536)[0]

    @property
    def seed(self) -> int:
        return struct.unpack_from("<i", self._buf, 1540)[0]

    @property
    def dynamic_area(self) -> float:
        return struct.unpack_from("<f", self._buf, 1544)[0]

    @property
    def use_reference(self) -> bool:
        return bool(self._buf[1548])

    @property
    def shutdown_flag(self) -> bool:
        return bool(self._buf[1549])

    @property
    def input_ready(self) -> bool:
        return bool(self._buf[1550])

    @property
    def output_ready(self) -> bool:
        return bool(self._buf[1551])

    # ── write properties ───────────────────────────────────────────────────

    @input_ready.setter
    def input_ready(self, value: bool):
        self._buf[1550] = int(value)

    @output_ready.setter
    def output_ready(self, value: bool):
        self._buf[1551] = int(value)

    @property
    def status(self) -> int:
        return self._buf[1552]

    @status.setter
    def status(self, value: int):
        self._buf[1552] = value

    def set_error(self, msg: str) -> None:
        self.status = STATUS_ERROR
        encoded = msg.encode("utf-8")[:255]
        self._buf[1553 : 1553 + len(encoded)] = encoded
        self._buf[1553 + len(encoded)] = 0

    @property
    def error_msg(self) -> str:
        raw = bytes(self._buf[1553:1809])
        return raw.split(b"\x00")[0].decode("utf-8", errors="replace")


def _announce(name: str, value: str) -> None:
    """Print a key=value line to stdout and flush immediately."""
    print(f"{name}={value}", flush=True)


def main():
    args = parse_args()
    w, h = args.width, args.height
    frame_bytes = h * w * 3  # BGR uint8

    # Create shared memory blocks
    shm_in  = shared_memory.SharedMemory(create=True, size=frame_bytes)
    shm_out = shared_memory.SharedMemory(create=True, size=frame_bytes)
    shm_ctrl = shared_memory.SharedMemory(create=True, size=CTRL_SIZE)

    # Zero-initialise control block
    shm_ctrl.buf[:CTRL_SIZE] = b"\x00" * CTRL_SIZE

    ctrl = CtrlBlock(shm_ctrl)

    # Announce names so C++ can attach
    _announce("FLUXRT_INPUT",  shm_in.name)
    _announce("FLUXRT_OUTPUT", shm_out.name)
    _announce("FLUXRT_CTRL",   shm_ctrl.name)
    print("FLUXRT_READY", flush=True)

    try:
        ctrl.status = STATUS_LOADING
        _run_inference(args, w, h, shm_in, shm_out, ctrl)
    except Exception as exc:
        ctrl.set_error(str(exc))
        sys.exit(1)
    finally:
        shm_in.close();   shm_in.unlink()
        shm_out.close();  shm_out.unlink()
        shm_ctrl.close(); shm_ctrl.unlink()


def _run_inference(args, w, h, shm_in, shm_out, ctrl: CtrlBlock):
    """Load StreamProcessor and run the main frame loop."""
    # (implemented in Task 3)
    ctrl.status = STATUS_RUNNING
    while not ctrl.shutdown_flag:
        time.sleep(0.01)


if __name__ == "__main__":
    main()
