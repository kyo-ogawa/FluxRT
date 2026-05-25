"""Tests for fluxrt_server shared memory protocol and CtrlBlock."""
import struct
import subprocess
import sys
import time
from multiprocessing import shared_memory

import numpy as np
import pytest

PYTHON = r"C:\Users\ogawa\miniconda3\envs\fluxrt\python.exe"
SERVER = r"td_plugin\fluxrt_server.py"


# ── CtrlBlock unit tests (no subprocess needed) ────────────────────────────

def _make_ctrl_shm():
    shm = shared_memory.SharedMemory(create=True, size=4096)
    shm.buf[:4096] = b"\x00" * 4096
    return shm


def test_ctrl_prompt_read():
    import os
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    from td_plugin.fluxrt_server import CtrlBlock
    shm = _make_ctrl_shm()
    ctrl = CtrlBlock(shm)
    msg = "hello world"
    encoded = msg.encode() + b"\x00"
    shm.buf[:len(encoded)] = encoded
    assert ctrl.prompt == msg
    shm.close()
    shm.unlink()


def test_ctrl_steps_read():
    import os
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    from td_plugin.fluxrt_server import CtrlBlock
    shm = _make_ctrl_shm()
    ctrl = CtrlBlock(shm)
    struct.pack_into("<i", shm.buf, 1536, 4)
    assert ctrl.steps == 4
    shm.close()
    shm.unlink()


def test_ctrl_status_write():
    import os
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    from td_plugin.fluxrt_server import CtrlBlock, STATUS_ERROR
    shm = _make_ctrl_shm()
    ctrl = CtrlBlock(shm)
    ctrl.set_error("test error")
    assert ctrl.status == STATUS_ERROR
    assert "test error" in ctrl.error_msg
    shm.close()
    shm.unlink()


def test_ctrl_lip_transfer_enable():
    import os
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    from td_plugin.fluxrt_server import CtrlBlock
    shm = _make_ctrl_shm()
    ctrl = CtrlBlock(shm)
    assert ctrl.lip_transfer_enable is False   # zero-initialised
    shm.buf[1809] = 1
    assert ctrl.lip_transfer_enable is True
    shm.buf[1809] = 0
    assert ctrl.lip_transfer_enable is False
    shm.close()
    shm.unlink()


# ── Subprocess protocol tests ─────────────────────────────────────────────

def _find_config():
    """Return path to any valid config JSON in configs/."""
    import os, glob
    configs = glob.glob(
        os.path.join(os.path.dirname(__file__), "..", "configs", "*.json")
    )
    assert configs, "No config files found in configs/"
    return os.path.normpath(configs[0])


def test_server_stdout_protocol():
    """Server must print FLUXRT_INPUT=, FLUXRT_OUTPUT=, FLUXRT_CTRL=, FLUXRT_READY."""
    import os
    cwd = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
    config = _find_config()
    proc = subprocess.Popen(
        [PYTHON, SERVER, "--width", "64", "--height", "64", "--config", config],
        stdout=subprocess.PIPE,
        text=True,
        cwd=cwd,
    )
    names = {}
    ready = False
    deadline = time.time() + 20.0
    try:
        while time.time() < deadline:
            line = proc.stdout.readline().strip()
            if not line:
                if proc.poll() is not None:
                    break
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                names[k] = v
            if line == "FLUXRT_READY":
                ready = True
                break
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    assert ready, f"Server did not print FLUXRT_READY within 20s. Lines seen: {names}"
    assert "FLUXRT_INPUT"  in names, "Missing FLUXRT_INPUT"
    assert "FLUXRT_OUTPUT" in names, "Missing FLUXRT_OUTPUT"
    assert "FLUXRT_CTRL"   in names, "Missing FLUXRT_CTRL"


def test_server_creates_valid_shm():
    """Named shared memory blocks announced by the server must be attachable."""
    import os
    cwd = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
    config = _find_config()
    proc = subprocess.Popen(
        [PYTHON, SERVER, "--width", "64", "--height", "64", "--config", config],
        stdout=subprocess.PIPE,
        text=True,
        cwd=cwd,
    )
    names = {}
    deadline = time.time() + 20.0
    try:
        while time.time() < deadline:
            line = proc.stdout.readline().strip()
            if not line:
                if proc.poll() is not None:
                    break
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                names[k] = v
            if line == "FLUXRT_READY":
                break

        assert "FLUXRT_INPUT"  in names, "Missing FLUXRT_INPUT"
        assert "FLUXRT_OUTPUT" in names, "Missing FLUXRT_OUTPUT"
        assert "FLUXRT_CTRL"   in names, "Missing FLUXRT_CTRL"

        shm_in = shared_memory.SharedMemory(name=names["FLUXRT_INPUT"])
        assert shm_in.size == 64 * 64 * 3
        shm_in.close()

        shm_ctrl = shared_memory.SharedMemory(name=names["FLUXRT_CTRL"])
        assert shm_ctrl.size == 4096
        shm_ctrl.close()

        shm_out = shared_memory.SharedMemory(name=names["FLUXRT_OUTPUT"])
        assert shm_out.size == 64 * 64 * 3
        shm_out.close()
    finally:
        proc.terminate()
        proc.wait(timeout=5)
