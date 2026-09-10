#!/usr/bin/env python3
"""Summarize optional MELEE_FLIP_PROFILE logs; timings are CPU wall time."""
import argparse
import csv
import json
from pathlib import Path
import re
import statistics


def read_frames(path, scene, stream="frames"):
    current_scene = -1
    frames = []
    lines = path.read_text(errors="replace").splitlines()
    if stream == "submission":
        tag = "[flip-thread-submit]"
    else:
        tag = "[flip-thread-present]" if any("[flip-thread-present]" in line for line in lines) else "[flip-profile]"
    for line in lines:
        match = re.search(r"\[input-test\] ready scene (-?\d+)", line)
        if match:
            current_scene = int(match[1])
        if tag in line and (scene is None or current_scene == scene):
            frames.append({key: float(value) for key, value in
                           re.findall(r"(\w+)=([\d.]+)", line)})
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--scene", type=int, help="Filter scripted-test scene (2 = match)")
    parser.add_argument("--csv", type=Path, help="Write individual frame measurements")
    parser.add_argument("--stream", choices=["frames", "submission"], default="frames",
                        help="Actual presentations, or independent threaded-render submission timings")
    parser.add_argument("--tail", type=int, help="Summarize only the last N matching measurements")
    args = parser.parse_args()
    frames = read_frames(args.log, args.scene, args.stream)
    if args.tail is not None:
        if args.tail < 1: parser.error("--tail must be positive")
        frames = frames[-args.tail:]
    if not frames:
        parser.error("No matching profile records found")
    if args.csv:
        with args.csv.open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=list(frames[0]))
            writer.writeheader()
            writer.writerows(frames)
    summary = {"frames": len(frames), "timing": "Milliseconds; CPU submit and GPU/presentation intervals can overlap and must not be added"}
    for key in frames[0]:
        values = sorted(frame[key] for frame in frames)
        summary[key] = {
            "mean": round(statistics.mean(values), 3),
            "median": round(statistics.median(values), 3),
            "p95": round(values[int((len(values) - 1) * .95)], 3),
        }
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
