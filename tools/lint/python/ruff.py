# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import platform
import re
import signal
import subprocess
import sys

from mozlint import result

here = os.path.abspath(os.path.dirname(__file__))


def default_bindir():
    # We use sys.prefix to find executables as that gets modified with
    # virtualenv's activate_this.py, whereas sys.executable doesn't.
    if platform.system() == "Windows":
        return os.path.join(sys.prefix, "Scripts")
    else:
        return os.path.join(sys.prefix, "bin")


def get_ruff_version(binary):
    """
    Returns found binary's version
    """
    try:
        output = subprocess.check_output(
            [binary, "--version"],
            stderr=subprocess.STDOUT,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        output = e.output

    matches = re.match(r"ruff ([0-9\.]+)", output)
    if matches:
        return matches[1]
    print(f"Error: Could not parse the version '{output}'")


def run_process(config, cmd, **kwargs):
    orig = signal.signal(signal.SIGINT, signal.SIG_IGN)
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True
    )
    signal.signal(signal.SIGINT, orig)
    try:
        output, _ = proc.communicate()
        proc.wait()
    except KeyboardInterrupt:
        proc.kill()

    return output


def lint(paths, config, log, **lintargs):
    fixed = 0
    results = []

    if not paths:
        return {"results": results, "fixed": fixed}

    args = ["ruff", "check", "--force-exclude"] + paths

    if config.get("exclude"):
        args.append(f"--extend-exclude={','.join(config['exclude'])}")

    process_kwargs = {"processStderrLine": lambda line: log.debug(line)}

    warning_rules = set(config.get("warning-rules", []))
    if lintargs.get("fix"):
        # Do a first pass with --fix-only as the json format doesn't return the
        # number of fixed issues.
        fix_args = args + ["--fix-only"]
        if not lintargs.get("warning"):
            # Don't fix warnings to limit unrelated changes sneaking into patches.
            # except when  --fix -W  is passed
            fix_args.append(f"--extend-ignore={','.join(warning_rules)}")

        log.debug(f"Running --fix: {fix_args}")
        output = run_process(config, fix_args, **process_kwargs)
        matches = re.match(r"Fixed (\d+) errors?.", output)
        if matches:
            fixed = int(matches[1])
    args += ["--output-format=json"]
    log.debug(f"Running with args: {args}")

    output = run_process(config, args, **process_kwargs)
    if not output:
        return []

    try:
        issues = json.loads(output)
    except json.JSONDecodeError:
        log.error(f"could not parse output: {output}")
        return []

    for issue in issues:
        res = {
            "path": issue["filename"],
            "lineno": issue["location"]["row"],
            "column": issue["location"]["column"],
            "lineoffset": issue["end_location"]["row"] - issue["location"]["row"],
            "message": issue["message"],
            "rule": issue["code"],
            "level": "error",
        }
        if any(issue["code"].startswith(w) for w in warning_rules):
            res["level"] = "warning"

        if issue["fix"]:
            res["hint"] = issue["fix"]["message"]

        results.append(result.from_config(config, **res))

    return {"results": results, "fixed": fixed}
