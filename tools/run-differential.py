#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Record named rejections and fail on any unexpected relay/content difference."""
import argparse
import json
import hashlib
import importlib.util
import pathlib
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--executable", type=pathlib.Path, required=True)
parser.add_argument("--reader", type=pathlib.Path, required=True)
parser.add_argument("--corpus", type=pathlib.Path, required=True)
parser.add_argument("--report", type=pathlib.Path, required=True)
args = parser.parse_args()
results, exceptions, failures = [], [], []
classification = {}
verified_streams = 0
spec = importlib.util.spec_from_file_location("capture", pathlib.Path(__file__).with_name("capture-corpus.py"))
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)
reasons = {3: "LIMIT", 4: "SYNTAX", 5: "UTF8", 6: "DUPLICATE_KEY"}
for manifest_path in sorted(args.corpus.glob("*/MANIFEST.json")):
    manifest = json.loads(manifest_path.read_text())
    for entry in manifest["streams"]:
        path = manifest_path.parent / entry["path"]
        if entry["source"] != manifest_path.parent.name:
            raise SystemExit("corpus protocol directory/provenance mismatch")
        blob = path.read_bytes()
        capture.admit(blob)
        if (path.resolve().parent != manifest_path.parent.resolve() or
            hashlib.sha256(blob).hexdigest() != entry["sha256"] or
            len(blob) != entry["bytes"] or blob.count(b"\n") != entry["lines"] or
            entry["anonymized"] is not False or not entry["tool"] or not entry["captured_at"]):
            raise SystemExit("corpus integrity/provenance mismatch")
        reader = subprocess.run([str(args.reader.resolve()), str(path), entry["source"]],
                                capture_output=True, check=True)
        measured = json.loads(reader.stdout)
        profile = "codex" if entry["source"] == "codex-app-server" else "strict"
        summary = classification.setdefault(entry["source"], {
            "streams": 0, "documents": 0, "missing_version": 0,
            "correlation": {field: 0 for field in ("opened", "settled", "duplicate_responses",
                "unknown_zero", "invalid_argument", "cancelled", "release_lost")},
            "classified_strict": {"accepted": 0, "invalid": 0},
            "classified_codex": {"accepted": 0, "invalid": 0}})
        summary["streams"] += 1
        for field in ("documents", "missing_version"):
            summary[field] += measured[field]
        for field in summary["correlation"]:
            summary["correlation"][field] += measured["correlation"][field]
        for dialect in ("strict", "codex"):
            for field in ("accepted", "invalid"):
                summary["classified_" + dialect][field] += measured["classified_" + dialect][field]
        if measured["classified_" + profile]["invalid"]:
            failures.append({"path": str(path), "reason": "INVALID_PROTOCOL_CLASSIFICATION"})
        if entry["source"] != "codex-app-server" and measured["classified_strict"] != measured["classified_codex"]:
            failures.append({"path": str(path), "reason": "PROFILE_DIVERGENCE"})
        verified_streams += 1
        run = subprocess.run([str(args.executable.resolve()), str(path), entry["source"]], capture_output=True, check=True)
        for line in run.stdout.splitlines():
            observation = json.loads(line)
            record = {"path": str(path), "source": entry["source"], **observation}
            results.append(record)
            legacy = "mcp" if entry["source"] == "mcp-stdio" else "codex"
            if observation["jsonrpc"] != 0:
                reason = reasons.get(observation["why"], "UNEXPECTED")
                exceptions.append({**record, "reason": reason})
                if reason == "UNEXPECTED": failures.append(record)
            elif not observation["classification_equal"]:
                failures.append({**record, "reason": "CLASSIFICATION_DIFFERENTIAL"})
            elif observation[legacy] and not observation["equal_" + legacy]:
                failures.append(record)
            elif observation["writer"] != 0:
                reason = "DEPENDENCY_WRITER_RANGE" if observation["writer"] == 8 else "UNEXPECTED_WRITER_FAILURE"
                exceptions.append({**record, "reason": reason})
                if reason == "UNEXPECTED_WRITER_FAILURE": failures.append(record)

args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps({"lines": len(results), "streams": len({r['path'] for r in results}),
    "exceptions": exceptions, "failures": failures, "classification": classification,
    "correlation_policy": {
        "table_scope": "one_per_descriptor_stream",
        "open_sequence": "1..maximum_integer_id_in_stream",
        "duplicate_responses": "UNKNOWN_ID_after_first_settlement",
        "zero_id_origin": "capture_client_not_built_on_calls",
        "zero_id_reason": "never_emitted_by_this_table_not_a_special_id",
        "calls_client_can_emit_zero": False,
        "shutdown": "cancel_until_AGAIN_then_release_returns_zero"},
    "corpus_present": bool(results), "verified_streams": verified_streams,
    "chunk_invariance": "byte_for_byte_documents_order_errors_stats_classification_and_finish"}, indent=2, sort_keys=True) + "\n")
print(f"differential: {len(results)} lines, {len(exceptions)} named exceptions, {len(failures)} failures")
if failures or not results:
    raise SystemExit(1)
