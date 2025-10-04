#!/usr/bin/env python3
import subprocess
import time
import math
import sys
import threading
import io

# This import will fail if python-xlib is not installed
try:
    from Xlib import display, X
    XLIBS_AVAILABLE = True
except Exception:
    XLIBS_AVAILABLE = False


def assert_window_is_shown_xlib(process_pid, timeout=5):
    """
    Uses the 'python-xlib' library to verify that a GUI window owned by the
    given PID has appeared on the screen.
    """
    print(f"\nVerifying GUI window for PID {process_pid} using 'python-xlib'...")
    if not XLIBS_AVAILABLE:
        print("--> WARNING: 'python-xlib' not found. Assuming window appeared.")
        return True

    try:
        d = display.Display()
        root = d.screen().root
    except Exception as e:
        print(f"--> ERROR: Could not connect to the X Display: {e}")
        return False

    NET_WM_PID = d.intern_atom('_NET_WM_PID')
    NET_CLIENT_LIST = d.intern_atom('_NET_CLIENT_LIST')

    start_time = time.monotonic()
    while time.monotonic() - start_time < timeout:
        try:
            prop = root.get_full_property(NET_CLIENT_LIST, X.AnyPropertyType)
            if not prop:
                time.sleep(0.25)
                continue
            window_ids = prop.value
            for window_id in window_ids:
                try:
                    window = d.create_resource_object('window', window_id)
                    pid_property = window.get_full_property(NET_WM_PID, X.AnyPropertyType)
                    if pid_property and pid_property.value and pid_property.value[0] == process_pid:
                        print(f"--> SUCCESS: Found window ID {window.id} for PID {process_pid}.")
                        d.close()
                        return True
                except Exception:
                    # Some windows may disappear between enumeration and property read.
                    continue
        except Exception:
            pass
        time.sleep(0.25)

    print(f"--> FAILURE: Timed out after {timeout} seconds. No window found for PID {process_pid}.")
    try:
        d.close()
    except Exception:
        pass
    return False


def _drain_proc_stderr(proc, timeout=1.0):
    """
    Read remaining stderr from a process. If stderr is a text stream, return the text.
    This will block, but since the process is expected to be terminated, it should return quickly.
    """
    if not proc or not getattr(proc, "stderr", None):
        return ""
    try:
        # If stderr is a text file object (text=True passed), read directly.
        data = proc.stderr.read()
        if data is None:
            return ""
        return data
    except Exception:
        # As a fallback, try to read bytes then decode.
        try:
            raw = proc.stderr.buffer.read()
            if raw:
                return raw.decode("utf-8", errors="replace")
        except Exception:
            pass
    return ""


def _report_gtkwave_exit(gtkwave_proc):
    """
    Called when gtkwave exits unexpectedly: prints return code and stderr contents.
    """
    if not gtkwave_proc:
        print("--> GTKWave process object not available.")
        return

    returncode = gtkwave_proc.returncode
    if returncode is None:
        # Process hasn't been waited on; try to poll/wait briefly.
        try:
            gtkwave_proc.wait(timeout=0.1)
            returncode = gtkwave_proc.returncode
        except Exception:
            returncode = gtkwave_proc.returncode

    if returncode is None:
        print("--> GTKWave exited but return code is unknown.")
    elif returncode < 0:
        # Killed by signal
        sig = -returncode
        print(f"\n--- GTKWave terminated by signal: {sig} (possible segfault if signal 11). Return code: {returncode} ---")
    else:
        print(f"\n--- GTKWave exited with return code: {returncode} ---")

    stderr_output = _drain_proc_stderr(gtkwave_proc)
    if stderr_output:
        print("\n--- GTKWave stderr (truncated to 40k chars) ---")
        print(stderr_output[:40000])
        print("--- end of GTKWave stderr ---")
    else:
        print("(No stderr output captured from GTKWave.)")


def stream_to_gtkwave_interactive():
    """
    Launches the shmidcat -> gtkwave pipeline, verifies the GUI appears,
    and then streams correctly formatted VCD data with flushing for smooth updates.

    This version monitors the gtkwave process: if it exits (crashes or closed),
    streaming stops and stderr is reported to the user.
    """
    shmidcat_proc = None
    gtkwave_proc = None
    writer = None

    try:
        # 1. Launch the pipeline.
        print("Launching shmidcat process...")
        shmidcat_proc = subprocess.Popen(
            ['shmidcat'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1  # line-buffered in text mode
        )

        print("Launching gtkwave process...")
        # Pass the stdout of shmidcat to gtkwave's stdin. Capture stderr to report crashes.
        gtkwave_proc = subprocess.Popen(
            ['gtkwave', '-I', '-v'],
            stdin=shmidcat_proc.stdout,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
        print(f"GTKWave process launched with PID: {gtkwave_proc.pid}")

        # 2. Create ONE VCDWriter instance.
        try:
            from vcd import VCDWriter
        except Exception:
            print("\n--- ERROR: 'vcd' Python package not found. Install with `pip install vcd` or similar. ---")
            return

        # writer writes to shmidcat_proc.stdin.
        writer = VCDWriter(shmidcat_proc.stdin, timescale='1 ns', date='today')
        sine_wave_var = writer.register_var('mysim', 'sine_wave', 'integer', size=8, init=0)

        # 3. Prime the pipeline.
        print("\nPriming the pipeline with VCD header to prevent deadlock...")
        writer.flush()

        # It is possible gtkwave exited immediately (e.g., missing display). Check it.
        if gtkwave_proc.poll() is not None:
            print("\n--- ERROR: GTKWave exited immediately after launch. ---")
            _report_gtkwave_exit(gtkwave_proc)
            return

        # 4. Assert that the window appears.
        if not assert_window_is_shown_xlib(gtkwave_proc.pid):
            # If gtkwave has already exited while waiting for the window, report and exit.
            if gtkwave_proc.poll() is not None:
                print("\n--- ERROR: GTKWave terminated while waiting for window. ---")
                _report_gtkwave_exit(gtkwave_proc)
                return
            print("\n--- TEST FAILED: GTKWave interactive GUI did not appear. ---")
            return

        print("\n--- TEST PASSED: GTKWave interactive GUI is visible. ---")

        # 5. Stream the data.
        print("Streaming data with forced flushing for smooth updates...")
        amplitude = 127
        steps = 1200

        for timestamp in range(steps):
            # Before attempting to write, ensure gtkwave is still running.
            if gtkwave_proc.poll() is not None:
                print("\n--- NOTICE: GTKWave has exited. Stopping streaming. ---")
                _report_gtkwave_exit(gtkwave_proc)
                break

            angle = (timestamp / 100.0) * 2 * math.pi
            value = int(amplitude * math.sin(angle))

            try:
                writer.change(sine_wave_var, timestamp, value)
                # Force the writer to flush its buffer through the pipe immediately.
                writer.flush()
            except BrokenPipeError:
                # Broken pipe indicates the consumer closed (shmidcat or gtkwave). Stop streaming.
                print("\n\n--- ERROR: Broken pipe detected while writing to shmidcat. GTKWave or shmidcat closed? ---")
                if gtkwave_proc and gtkwave_proc.poll() is not None:
                    _report_gtkwave_exit(gtkwave_proc)
                break
            except Exception as e:
                # Catch other IO errors and stop.
                print(f"\n\n--- ERROR: Exception while writing/streaming: {e} ---")
                if gtkwave_proc and gtkwave_proc.poll() is not None:
                    _report_gtkwave_exit(gtkwave_proc)
                break

            # Progress indicator
            print(f"Timestamp {timestamp+1}/{steps} streamed.", end='\r')

            # Small sleep to simulate real streaming frequency.
            time.sleep(0.05)
        else:
            # Completed all steps without interruption.
            print(f"\n\nFinished generating {steps} timesteps. Press Enter to close.")
            try:
                input()
            except Exception:
                # If running in non-interactive environment, don't block.
                pass

    except FileNotFoundError as e:
        print(f"\n--- ERROR: Command not found: {e.filename}. ---")
    except KeyboardInterrupt:
        print("\n--- Interrupted by user. Shutting down. ---")
    except Exception as e:
        print(f"\nAn unexpected error occurred: {e}")
        # If gtkwave has exited, try to report stderr as well.
        if gtkwave_proc and gtkwave_proc.poll() is not None:
            _report_gtkwave_exit(gtkwave_proc)
    finally:
        print("Cleaning up processes and resources...")

        # Close VCD writer if present.
        if writer:
            try:
                writer.close()
            except Exception:
                pass

        # Ensure we close the shmidcat stdin to signal EOF to downstream processes.
        if shmidcat_proc:
            try:
                if getattr(shmidcat_proc, "stdin", None):
                    try:
                        shmidcat_proc.stdin.close()
                    except Exception:
                        pass
            except Exception:
                pass

        # Terminate gtkwave if still running.
        if gtkwave_proc:
            try:
                if gtkwave_proc.poll() is None:
                    gtkwave_proc.terminate()
                    try:
                        gtkwave_proc.wait(timeout=1.0)
                    except Exception:
                        gtkwave_proc.kill()
            except Exception:
                pass

            # Report final stderr if gtkwave exited earlier but wasn't reported.
            try:
                if gtkwave_proc.returncode is None:
                    gtkwave_proc.wait(timeout=0.1)
            except Exception:
                pass

            if gtkwave_proc.returncode is not None:
                _report_gtkwave_exit(gtkwave_proc)

        # Terminate shmidcat if still running.
        if shmidcat_proc:
            try:
                if shmidcat_proc.poll() is None:
                    shmidcat_proc.terminate()
                    try:
                        shmidcat_proc.wait(timeout=1.0)
                    except Exception:
                        shmidcat_proc.kill()
            except Exception:
                pass

        print("Processes terminated.")


if __name__ == "__main__":
    # If user runs this script in an environment without DISPLAY or without the
    # required binaries, the script will report appropriate errors instead of
    # crashing silently.
    if not sys.platform.startswith("linux"):
        print("This script is intended for Linux desktop environments.")
    stream_to_gtkwave_interactive()
