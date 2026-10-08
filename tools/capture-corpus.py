#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Capture synthetic sessions through real binaries, with admission before writing.

Each descriptor direction is kept byte-for-byte in its own stream; session_id
links the pair. No rewriting/anonymization, private history or credential reads.
Gemini CLI absence is recorded; its upstream executable fixture is explicitly
labelled fixture, not a real Gemini session.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import selectors
import shutil
import subprocess
import tempfile
import time

LIMIT = 16 * 1048576
SECRET = re.compile(rb"(?:sk-[A-Za-z0-9_-]{12,}|(?:Bearer|Basic)\s+[A-Za-z0-9+/=_-]{12,}|-----BEGIN .*PRIVATE KEY|\"(?:access_token|refresh_token|id_token|api_key|apiKey|authorization)\"\s*:\s*\"[^\"]+\")", re.I)
PERSONAL = re.compile(rb"(?:/Users/|/home/|[A-Za-z]:\\\\Users\\\\)", re.I)
STAMP = datetime.datetime.now(datetime.timezone.utc).isoformat()


def admit(blob):
    if PERSONAL.search(blob):
        raise ValueError("personal_path")
    if SECRET.search(blob):
        raise ValueError("credential_pattern")
    # Inspect decoded strings as well: escaped slashes must not bypass admission.
    def inspect(value):
        if isinstance(value, str):
            decoded = value.encode("utf-8")
            if PERSONAL.search(decoded) or re.search(rb"[A-Za-z]:\\Users\\", decoded, re.I):
                raise ValueError("personal_path")
            if SECRET.search(decoded):
                raise ValueError("credential_pattern")
        elif isinstance(value, list):
            for child in value: inspect(child)
        elif isinstance(value, dict):
            for key, child in value.items():
                if key.lower() in {"access_token", "refresh_token", "id_token", "api_key", "apikey", "authorization"} and isinstance(child, str) and child:
                    raise ValueError("credential_pattern")
                inspect(key); inspect(child)
    # A valid quoted string in an otherwise malformed JSON line is still
    # inspected: protocol refusal must not bypass corpus privacy admission.
    for literal in re.finditer(rb'"(?:[^"\\]|\\.)*"', blob):
        try: value = json.loads(literal.group())
        except (ValueError, UnicodeError): continue
        inspect(value)
    for row in blob.splitlines():
        try: value = json.loads(row)
        except (ValueError, UnicodeError): continue
        inspect(value)


class Recorder:
    def __init__(self, command, cwd, environment=None):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, cwd=cwd, env=environment, bufsize=0)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.sent, self.received, self.partial = bytearray(), bytearray(), bytearray()
        self.messages = []
        self.id = 0
        self.approvals_declined = 0

    def send(self, message):
        blob = json.dumps(message, separators=(",", ":"), ensure_ascii=False).encode() + b"\n"
        admit(blob)
        self.sent.extend(blob)
        self.process.stdin.write(blob)

    def read(self, seconds):
        if not self.selector.select(seconds):
            return []
        blob = os.read(self.process.stdout.fileno(), 65536)
        if not blob:
            return []
        self.received.extend(blob)
        if len(self.received) > LIMIT:
            raise ValueError("capture_memory_limit")
        self.partial.extend(blob)
        output = []
        while b"\n" in self.partial:
            row, _, rest = self.partial.partition(b"\n")
            self.partial = bytearray(rest)
            try:
                message = json.loads(row)
            except (ValueError, UnicodeError):
                continue
            output.append(message)
            self.messages.append(message)
            if message.get("method") == "item/commandExecution/requestApproval" and "id" in message:
                self.send({"id": message["id"], "result": {"decision": "decline"}})
                self.approvals_declined += 1
        return output

    def request(self, method, params, jsonrpc=True, timeout=10):
        self.id += 1
        message = {"id": self.id, "method": method, "params": params}
        if jsonrpc:
            message["jsonrpc"] = "2.0"
        self.send(message)
        wanted = self.id
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for response in self.read(min(0.2, end - time.monotonic())):
                if response.get("id") == wanted and ("result" in response or "error" in response):
                    return response
            if self.process.poll() is not None:
                break
        return None

    def finish(self):
        if not self.process.stdin.closed:
            self.process.stdin.close()
        end = time.monotonic() + 1
        while self.process.poll() is None and time.monotonic() < end:
            self.read(0.05)
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        # Drain any final descriptor bytes, preserving fragments and orphan bytes.
        while self.selector.select(0):
            blob = os.read(self.process.stdout.fileno(), 65536)
            if not blob:
                break
            self.received.extend(blob)
            if len(self.received) > LIMIT:
                raise ValueError("capture_memory_limit")
        self.selector.close()
        self.process.stdout.close()
        return bytes(self.sent), bytes(self.received)


def record_pair(directory, manifest, name, recorder, source, tool, provenance):
    incoming, outgoing = recorder.finish()
    # Both directions must pass admission before either is written.
    admit(incoming); admit(outgoing)
    if not outgoing:
        raise ValueError("empty_binary_output")
    for direction, blob in (("client-to-server", incoming), ("server-to-client", outgoing)):
        filename = f"{name}-{direction}.jsonl"
        (directory / filename).write_bytes(blob)
        manifest["streams"].append({"path": filename, "session_id": name, "direction": direction,
            "source": source, "captured_at": STAMP, "tool": tool, "provenance": provenance,
            "lines": blob.count(b"\n"), "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
            "admission": "no_personal_path_or_credential_pattern", "anonymized": False})


def manifest():
    return {"schema_version": 2, "directive_revision": "2.2", "captured_at": STAMP,
            "streams": [], "rejections": [], "notes": []}


def capture_mcp(args):
    directory = args.output / "mcp-stdio"; directory.mkdir(parents=True, exist_ok=True)
    report = manifest(); binary = args.mcp / "build/release/bin"
    names = ["example-provider", "sdk-prompts-provider", "legacy-mcp-upstream",
             "chatty-mcp-upstream", "exotic-schema-mcp-upstream"]
    for index in range(25):
        name = names[index % len(names)]
        command = [str(binary / name)] if "upstream" in name else [str(binary / "maelys-mcp"), "--provider", str(binary / name)]
        recorder = Recorder(command, "/private/tmp")
        if "upstream" in name:
            recorder.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                "clientInfo": {"name": "jsonrpc-corpus", "version": "0"}})
            recorder.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
            recorder.request("server/discover", {})
            recorder.request("tools/list", {})
            recorder.request("tools/call", {"name": "legacy.echo" if "legacy" in name else "proxy.probe",
                "arguments": {"case": index, "text": "synthetic quote\" newline\n é", "number": 1.5}})
        else:
            recorder.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                "clientInfo": {"name": "jsonrpc-corpus", "version": "0"}})
            recorder.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
            recorder.request("tools/list", {})
            recorder.request("tools/call", {"name": "example.echo", "arguments": {"message": f"synthetic-{index} quote\" é"}})
            recorder.request("prompts/list", {})
            recorder.request("prompts/get", {"name": "example.summarize", "arguments": {"subject": "synthetic"}})
        try:
            record_pair(directory, report, f"mcp-{index:03d}", recorder, "mcp-stdio",
                        f"maelys-mcp v1.1.1/{name}", "real_example_or_test_provider_binary")
        except ValueError as error:
            report["rejections"].append({"session": index, "reason": str(error)})
    seeds = args.mcp / "fuzz/seeds/json-lines"
    for index, source in enumerate(sorted(seeds.glob("*.jsonl"))):
        blob = source.read_bytes()
        try:
            admit(blob)
        except ValueError as error:
            report["rejections"].append({"seed": source.name, "reason": str(error)}); continue
        name = f"upstream-seed-{index:03d}.jsonl"; (directory / name).write_bytes(blob)
        report["streams"].append({"path": name, "source": "mcp-stdio", "captured_at": STAMP,
            "tool": "maelys-mcp v1.1.1 fuzz/seeds/json-lines", "provenance": "upstream_fuzz_fixture",
            "lines": blob.count(b"\n"), "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
            "admission": "no_personal_path_or_credential_pattern", "anonymized": False})
    (directory / "MANIFEST.json").write_text(json.dumps(report, indent=2, sort_keys=True)+"\n")
    return report


def capture_gemini(args):
    directory = args.output / "gemini-acp"; directory.mkdir(parents=True, exist_ok=True)
    report = manifest()
    report["notes"].append("Gemini CLI absent locally; executable upstream fixtures only, not real Gemini captures.")
    modes = ["normal", "partial", "noise_before_jsonrpc", "initialize_version_mismatch",
             "initialize_missing_agent_capabilities", "session_new_error", "new_session_missing_id",
             "session_load_error", "prompt_method_not_found", "prompt_multiple_session_updates"]
    for index, mode in enumerate(modes):
        recorder = Recorder([str(args.gemini_fixture.resolve()), mode], "/private/tmp")
        recorder.request("initialize", {"protocolVersion": 1, "clientCapabilities": {},
            "clientInfo": {"name": "jsonrpc-corpus", "version": "0"}})
        recorder.request("session/new", {"cwd": "/private/tmp/jsonrpc-corpus-synthetic", "mcpServers": []})
        recorder.request("session/load", {"sessionId": "gemini-session-new", "cwd": "/private/tmp/jsonrpc-corpus-synthetic", "mcpServers": []})
        recorder.request("session/prompt", {"sessionId": "gemini-session-new", "prompt": [{"type": "text", "text": f"synthetic-{index}"}]})
        try:
            record_pair(directory, report, f"gemini-{index:03d}", recorder, "gemini-acp",
                        "codexmanager 600eb55/fake_gemini_acp_server", "executable_fixture_cli_absent")
        except ValueError as error:
            report["rejections"].append({"session": index, "reason": str(error)})
    (directory / "MANIFEST.json").write_text(json.dumps(report, indent=2, sort_keys=True)+"\n")
    return report


def capture_codex(args):
    directory = args.output / "codex-app-server"; directory.mkdir(parents=True, exist_ok=True)
    report = manifest(); report["notes"].append("Only synthetic cwd-filtered history; no private thread listing/read.")
    version = subprocess.run(["codex", "--version"], capture_output=True, check=True).stdout.decode().strip()
    cwd = pathlib.Path("/private/tmp/jsonrpc-corpus-synthetic"); cwd.mkdir(exist_ok=True)
    # Configure only child processes' documented Codex state directory. The
    # caller's HOME/CODEX_HOME and its credentials/settings are never changed.
    task_state = tempfile.TemporaryDirectory(prefix="jsonrpc-codex-state-", dir="/private/tmp")
    task_environment = dict(os.environ)
    task_environment["CODEX_HOME"] = task_state.name
    if args.use_existing_login:
        login = pathlib.Path(os.environ.get("CODEX_HOME", str(pathlib.Path.home() / ".codex"))) / "auth.json"
        if not login.is_file(): raise ValueError("local_login_file_absent")
        # The CLI consumes its existing login; no token contents enter this tool.
        (pathlib.Path(task_state.name) / "auth.json").symlink_to(login)
        report["notes"].append("Existing local login consumed by CLI through a temporary symlink; token contents are never read or printed by the recorder.")
    else:
        report["notes"].append("Fresh child-process Codex state; no credentials copied. A turn may fail authentication; approval capture is reported separately.")
    for index in range(26):
        recorder = Recorder(["codex", "app-server", "--stdio"], str(cwd), task_environment)
        recorder.request("initialize", {"clientInfo": {"name": "jsonrpc-corpus", "version": "0"},
            "capabilities": {"experimentalApi": True}}, jsonrpc=False)
        recorder.send({"method": "initialized"})
        recorder.request("thread/list", {"cwd": str(cwd), "limit": 1}, jsonrpc=False)
        response = recorder.request("thread/start", {"cwd": str(cwd), "ephemeral": True,
            "approvalPolicy": "untrusted", "sandbox": "read-only",
            "baseInstructions": "Synthetic protocol test. Do not read private files or histories."}, jsonrpc=False)
        thread = ((response or {}).get("result") or {}).get("thread", {}).get("id")
        if thread:
            recorder.request("thread/read", {"threadId": thread, "includeTurns": False}, jsonrpc=False)
            if index == 0 and args.turn:
                recorder.request("turn/start", {"threadId": thread, "input": [{"type": "text",
                    "text": "Synthetic protocol test: request an approval to run touch /private/tmp/jsonrpc-corpus-approval-probe with escalated permissions. Do not read files. I will decline. Stop after the refusal.",
                    "text_elements": []}]}, jsonrpc=False)
                end = time.monotonic() + 60
                while time.monotonic() < end:
                    messages = recorder.read(0.2)
                    if any(m.get("method") in ("turn/completed", "error") for m in messages): break
                report["approvals_declined"] = recorder.approvals_declined
            recorder.request("thread/close", {"threadId": thread}, jsonrpc=False)
        try:
            record_pair(directory, report, f"codex-{index:03d}", recorder, "codex-app-server", version,
                        "real_codex_binary_synthetic_session")
        except ValueError as error:
            report["rejections"].append({"session": index, "reason": str(error)})
    (directory / "MANIFEST.json").write_text(json.dumps(report, indent=2, sort_keys=True)+"\n")
    task_state.cleanup()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("protocol", choices=("mcp", "gemini", "codex"))
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("tests/corpus"))
    parser.add_argument("--mcp", type=pathlib.Path)
    parser.add_argument("--gemini-fixture", type=pathlib.Path)
    parser.add_argument("--turn", action="store_true")
    parser.add_argument("--use-existing-login", action="store_true")
    args = parser.parse_args()
    if args.protocol == "gemini" and shutil.which("gemini"):
        raise SystemExit("Gemini CLI available: use real gated sessions instead of the absence fallback")
    result = {"mcp": capture_mcp, "gemini": capture_gemini, "codex": capture_codex}[args.protocol](args)
    print(json.dumps({"protocol": args.protocol, "streams": len(result["streams"]),
                      "rejections": result["rejections"], "notes": result["notes"]}))


if __name__ == "__main__":
    main()
