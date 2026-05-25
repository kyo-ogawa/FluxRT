"""FluxRT inference server for TouchDesigner C++ TOP.

Protocol:
  stdout line 1: FLUXRT_INPUT=<shm_name>
  stdout line 2: FLUXRT_OUTPUT=<shm_name>
  stdout line 3: FLUXRT_CTRL=<shm_name>
  stdout line 4: FLUXRT_READY
  (then runs until ctrl.shutdown_flag is set)

Log file: <workdir>/fluxrt_server.log
"""
import argparse
import logging
import os
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


def _setup_logging(workdir: str) -> logging.Logger:
    log_path = os.path.join(workdir, "fluxrt_server.log")
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s %(levelname)s %(message)s",
        handlers=[
            logging.FileHandler(log_path, encoding="utf-8"),
            logging.StreamHandler(sys.stderr),
        ],
    )
    log = logging.getLogger("fluxrt")
    log.info("=== FluxRT server starting === log: %s", log_path)
    return log


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--width",   type=int, required=True)
    p.add_argument("--height",  type=int, required=True)
    p.add_argument("--int8",    action="store_true")
    p.add_argument("--config",  default="configs/config_with_reference.json")
    p.add_argument("--workdir", default=".")
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

    @property
    def lip_transfer_enable(self) -> bool:
        return bool(self._buf[1809])

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

    log = _setup_logging(args.workdir)
    log.info("Resolution: %dx%d  int8=%s  config=%s", w, h, args.int8, args.config)

    # Create shared memory blocks
    shm_in   = shared_memory.SharedMemory(create=True, size=frame_bytes)
    shm_out  = shared_memory.SharedMemory(create=True, size=frame_bytes)
    shm_ctrl = shared_memory.SharedMemory(create=True, size=CTRL_SIZE)

    # Zero-initialise control block
    shm_ctrl.buf[:CTRL_SIZE] = b"\x00" * CTRL_SIZE

    ctrl = CtrlBlock(shm_ctrl)

    # Announce names so C++ can attach
    _announce("FLUXRT_INPUT",  shm_in.name)
    _announce("FLUXRT_OUTPUT", shm_out.name)
    _announce("FLUXRT_CTRL",   shm_ctrl.name)
    print("FLUXRT_READY", flush=True)
    log.info("Shared memory announced — INPUT=%s OUTPUT=%s CTRL=%s",
             shm_in.name, shm_out.name, shm_ctrl.name)

    try:
        ctrl.status = STATUS_LOADING
        _run_inference(args, w, h, shm_in, shm_out, ctrl, log)
    except Exception as exc:
        log.exception("Fatal error in inference: %s", exc)
        ctrl.set_error(str(exc))
        sys.exit(1)
    finally:
        log.info("Cleaning up shared memory")
        shm_in.close();   shm_in.unlink()
        shm_out.close();  shm_out.unlink()
        shm_ctrl.close(); shm_ctrl.unlink()
        log.info("=== FluxRT server stopped ===")


def _run_inference(args, w, h, shm_in, shm_out, ctrl: CtrlBlock,
                   log: logging.Logger):
    """Load StreamProcessor and run the main frame loop."""
    import json
    import tempfile
    from PIL import Image

    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from fluxrt import StreamProcessor

    with open(args.config) as f:
        cfg = json.load(f)

    # Override resolution — config uses "resolution": {"height": H, "width": W}
    if "resolution" in cfg:
        cfg["resolution"]["width"]  = w
        cfg["resolution"]["height"] = h
    else:
        cfg["resolution"] = {"width": w, "height": h}

    if args.int8:
        cfg["enable_int8_quantization"] = True

    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as tmp:
        json.dump(cfg, tmp)
        tmp_config = tmp.name

    try:
        log.info("Loading StreamProcessor …")
        ctrl.status = STATUS_LOADING
        sp = StreamProcessor(tmp_config)
        if args.int8:
            sp.enable_quantization()
            log.info("int8 quantization enabled")

        log.info("Starting subprocesses (TensorRT compile on first run ~3 min) …")
        ctrl.status = STATUS_COMPILING
        sp.start()

        input_tensor  = sp.get_input_tensor()
        output_tensor = sp.get_output_tensor()

        # Warm up: write a black frame and wait for first output
        log.info("Warming up (waiting for first output frame) …")
        dummy = np.zeros((h, w, 3), dtype=np.uint8)
        input_tensor.copy_from(dummy)
        t0 = time.time()
        while not sp.is_ready() and not ctrl.shutdown_flag:
            time.sleep(0.5)
        log.info("Warm-up complete in %.1f s", time.time() - t0)

        if ctrl.shutdown_flag:
            log.info("Shutdown requested during warm-up — exiting")
            return

        ctrl.status = STATUS_RUNNING

        # numpy views over shared memory (no extra copy)
        in_arr  = np.ndarray((h, w, 3), dtype=np.uint8, buffer=shm_in.buf)
        out_arr = np.ndarray((h, w, 3), dtype=np.uint8, buffer=shm_out.buf)

        # Parameter snapshot for change detection
        last_prompt = ""
        last_steps  = -1
        last_seed   = -1
        last_ref    = ""
        last_lip    = False

        initial_prompt = ctrl.prompt or cfg.get("default_prompt", "Turn this into oil on canvas art")
        sp.set_prompt(initial_prompt)
        log.info("Running — prompt='%s'  steps=%d  seed=%d  dynamic_area=%.3f",
                 initial_prompt, ctrl.steps, ctrl.seed, ctrl.dynamic_area)

        frames_sent    = 0
        frames_output  = 0
        last_log_time  = time.time()

        while not ctrl.shutdown_flag:
            # ── Sync parameters ──────────────────────────────────────────
            p = ctrl.prompt
            if p != last_prompt:
                sp.set_prompt(p)
                last_prompt = p
                log.info("Prompt changed → '%s'", p)

            s = ctrl.steps
            if s > 0 and s != last_steps:
                sp.set_steps(s)
                last_steps = s
                log.info("Steps → %d", s)

            seed = ctrl.seed
            if seed != last_seed and seed > 0:
                sp.set_seed(seed)
                last_seed = seed
                log.info("Seed → %d", seed)

            ref = ctrl.reference_image_path
            if ref != last_ref and ctrl.use_reference:
                if ref and os.path.isfile(ref):
                    img = np.array(Image.open(ref).convert("RGB"))
                    sp.set_reference_image(img)
                    log.info("Reference image → %s", ref)
                elif not ref:
                    sp.set_reference_image(None)
                    log.info("Reference image cleared")
                last_ref = ref

            lip = ctrl.lip_transfer_enable
            if lip != last_lip:
                sp.set_lip_transfer(lip)
                last_lip = lip
                log.info("LipTransfer → %s", lip)

            # ── Input frame ──────────────────────────────────────────────
            if ctrl.input_ready:
                input_tensor.copy_from(in_arr)
                ctrl.input_ready = False
                frames_sent += 1

            # ── Output frame ─────────────────────────────────────────────
            out = output_tensor.to_numpy()
            np.copyto(out_arr, out)
            ctrl.output_ready = True
            frames_output += 1

            # Periodic status log (~every 10 s)
            now = time.time()
            if now - last_log_time >= 10.0:
                log.info("Alive — frames_sent=%d frames_output=%d",
                         frames_sent, frames_output)
                last_log_time = now

            time.sleep(0.001)

        log.info("Shutdown flag received — frames_sent=%d frames_output=%d",
                 frames_sent, frames_output)
    finally:
        log.info("Stopping StreamProcessor …")
        sp.stop()
        log.info("StreamProcessor stopped")
        os.unlink(tmp_config)


if __name__ == "__main__":
    main()
