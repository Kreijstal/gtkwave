#!/usr/bin/env python3
"""
Slow streaming demo to show real-time end time updates in GTKWave
"""

import subprocess
import time
import math
from vcd.writer import VCDWriter
import threading
import sys

def slow_stream_with_updates():
    """Stream data slowly to see end time updates"""
    shmidcat_proc = None
    gtkwave_proc = None

    try:
        print("Starting shmidcat process...")
        shmidcat_proc = subprocess.Popen(
            ['build/src/helpers/shmidcat'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1
        )

        print("Starting GTKWave in interactive mode...")
        gtkwave_proc = subprocess.Popen(
            ['xvfb-run', '-a', 'build/src/gtkwave', '-I', '-v'],
            stdin=shmidcat_proc.stdout,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )

        print("Creating VCD writer...")
        writer = VCDWriter(shmidcat_proc.stdin, timescale='1 ns', date='today')
        sine_var = writer.register_var('test', 'sine_wave', 'integer', size=8, init=0)

        print("Priming pipeline with VCD header...")
        writer.flush()

        # Give GTKWave time to start
        time.sleep(2)

        print("Starting slow streaming (1 timestep per second)...")
        print("=" * 50)

        amplitude = 127
        steps = 30  # Stream 30 steps slowly

        # Thread to capture GTKWave stdout
        def capture_stdout():
            while True:
                line = gtkwave_proc.stdout.readline()
                if not line:
                    break
                print(f"GTKWave STDOUT: {line.strip()}")

        # Thread to capture GTKWave stderr
        def capture_stderr():
            while True:
                line = gtkwave_proc.stderr.readline()
                if not line:
                    break
                print(f"GTKWave STDERR: {line.strip()}")

        stdout_thread = threading.Thread(target=capture_stdout, daemon=True)
        stderr_thread = threading.Thread(target=capture_stderr, daemon=True)
        stdout_thread.start()
        stderr_thread.start()

        # Stream data slowly
        for timestamp in range(steps):
            angle = (timestamp / 5.0) * 2 * math.pi
            value = int(amplitude * math.sin(angle))
            writer.change(sine_var, timestamp, value)
            writer.flush()

            print(f"Sent timestamp {timestamp+1}/{steps} (value: {value})")
            time.sleep(1)  # Slow down to 1 second per timestep

        print("\nFinished streaming. Keeping GTKWave open for 5 more seconds...")
        time.sleep(5)

        print("Demo completed successfully!")

    except Exception as e:
        print(f"Error: {e}")
    finally:
        print("Cleaning up...")
        if 'writer' in locals():
            writer.close()
        if gtkwave_proc:
            gtkwave_proc.terminate()
        if shmidcat_proc:
            shmidcat_proc.terminate()

if __name__ == "__main__":
    slow_stream_with_updates()
