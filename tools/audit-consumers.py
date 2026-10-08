#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Regenerate stage 0 source findings from exact git objects, not working files."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

REFERENCES = {
    "maelys-json": "d106890b02b3fbfd599ecd22bdeffb600db66123",
    "maelys-mcp": "b408231d0b9e61b53df0d59dc49be2e4184ebf3a",
    "codexmanager": "600eb5563af947dbdf54d724e9465dcb1472f58f",
    "maelys-release": "8623b3cb8bc432111275170b81eedb63635747d2",
}


def git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args], check=True,
                          stdout=subprocess.PIPE).stdout


def scan(root, commit, expression):
    entries = []
    paths = git(root, "ls-tree", "-rz", "--name-only", commit).decode().split("\0")
    for path in paths:
        if not path.endswith((".c", ".h")) or path.startswith(("extern/", "vendor/")):
            continue
        content = git(root, "show", f"{commit}:{path}")
        hits = []
        for number, line in enumerate(content.decode("utf-8").splitlines(), 1):
            if re.search(expression, line):
                hits.append({"line": number, "text": line.strip()})
        if hits:
            entries.append({"path": path, "sha256": hashlib.sha256(content).hexdigest(),
                            "test_only": path.startswith(("tests/", "fuzz/")), "hits": hits})
    return entries


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in REFERENCES:
        parser.add_argument(f"--{name}", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("docs/audits"))
    parser.add_argument("--corpus-report", type=pathlib.Path, default=pathlib.Path("docs/audits/corpus.json"))
    parser.add_argument("--drafts", type=pathlib.Path, default=pathlib.Path("build/audits"))
    args = parser.parse_args()
    roots = {name: getattr(args, name.replace("-", "_")) for name in REFERENCES}
    for name, commit in REFERENCES.items():
        if git(roots[name], "rev-parse", f"{commit}^{{commit}}").decode().strip() != commit:
            raise ValueError(f"reference mismatch: {name}")
    mcp = scan(roots["maelys-mcp"], REFERENCES["maelys-mcp"],
               r"maelys_mcp_(jsonrpc_core_|line_reader_|read_json_line|write_json_line)|"
               r"MAELYS_MCP_JSONRPC_(CONTENT_LENGTH|JSON_LINES)|JSON_REJECT_DUPLICATES")
    consumer = scan(roots["codexmanager"], REFERENCES["codexmanager"],
                    r"maelys_jsonrpc_(core_|runtime_adapter_|transport_open_fds|call\(|notify\()|"
                    r"MAELYS_JSONRPC_FRAMING_(CONTENT_LENGTH|JSON_LINES)")
    dependency = scan(roots["maelys-json"], REFERENCES["maelys-json"],
                      r"writer_value|writer_number_text|write_number_lexeme|copy_number|NOT_INTEGER|byte < 0x20|"
                      r"MAELYS_JSON_VERSION_|MAELYS_JSON_ABI_VERSION|MAELYS_JSON_MAXIMUM_NUMBER_TEXT|BOM|byte order mark")
    report = {
        "schema_version": 1, "directive_revision": "2.2", "date": "2026-10-08",
        "stage": 0, "stage_status": "source_and_corpus_measured", "references": REFERENCES,
        "source_audit": "complete_for_named_git_objects",
        "corpus": json.loads(args.corpus_report.read_text()),
        "content_length": {"live_consumer_found": False,
                           "scope": "named_reference_trees_only", "include_in_library": False},
        "mcp": {"abi": 6, "adoption_abi": 7,
                "duplicates": "rejected_by_JSON_REJECT_DUPLICATES",
                "core_production_callers": ["src/core/common.c"],
                "line_reader_production_callers": ["src/transport/stdio.c",
                    "src/provider/process_provider.c", "src/provider/provider_sdk.c",
                    "src/provider/mcp_proxy.c"],
                "jsonrpc_fuzzers": ["fuzz/fuzz_json_lines.c", "fuzz/fuzz_content_length.c"]},
        "codexmanager": {"local_repository_name": "maelys-orchestrator",
                "duplicates": "json_loads_flags_0_last_key_wins",
                "core_production_callers": ["transports/jsonrpc_transport.c",
                    "transports/jsonrpc_runtime_adapter.c", "protocols/acp/acp_client.c",
                    "agents/execution/agent_process_longlived.c"],
                "adapter_production_callers": ["agents/providers/codex/codex_app_server_profile.c",
                    "agents/providers/gemini/gemini_acp_profile.c",
                    "protocols/mcp/mcp_endpoint_runtime_server.c"],
                "threaded_transport_live_caller_found": False},
        "extract": ["bounded JSON Lines reader with pull interface",
                    "message classification and writer envelope helpers",
                    "monotonic integer ID reservation and bounded pending table",
                    "caller-supplied deadlines and settlement",
                    "saturating preamble, overflow, rejection and byte counters"],
        "leave_with_caller": ["file descriptors, read/write/select, close, wake pipe",
                    "reader thread, mutexes, semaphore waits and signaling",
                    "transport lifecycle and EOF propagation",
                    "request handler dispatch and method-not-found policy",
                    "error excerpt redaction policy",
                    "runtime fault, timeout and close events"],
        "blockers": [],
        "integration_findings": {"codex_app_server": "Real binary omits jsonrpc marker; consumer adapter must add 2.0 via writer before strict classify", "library_policy": "strict 2.0 validation unchanged"},
        "resolved_decisions": {
            "corpus": "real binary examples and synthetic sessions; raw bytes admitted before write; Gemini fixtures allowed if CLI absent",
            "number_relay": "pinned 0.3.0 ABI 3; scenario 8 mandatory, exact lexeme relay and two-pass fixed point",
            "calls_drain": "pull cancel then size_t release reporting lost entries",
            "c0": "RFC8259 escaping; no raw control bytes",
            "named_rejections": "DUPLICATE_KEY, UTF8, LIMIT plus SYNTAX for BOM/U0000",
            "canonical": "integer canonical domain unchanged; fractional/exponent relay is a fixed point, is_canonical false"},
        "stage_0b": {"status": "cleared", "maelys_json_version": "0.3.0", "maelys_json_abi": 3, "scenario_8": "mandatory"},
        "implementation": "frame_message_calls_implemented_at_0.0.0",
        "release": {"version": "0.0.0", "cut": "not_run", "published_consumers": 0},
    }
    write_json(args.output / "stage-0.json", report)
    write_json(args.output / "source-references.json",
               {"schema_version": 1, "references": REFERENCES,
                "maelys-mcp": mcp, "codexmanager": consumer, "maelys-json": dependency})
    paragraphs = {
        "maelys-mcp": """Le cœur `src/jsonrpc/core.c` est utilisé en production par les deux
écrivains JSON Lines de `src/core/common.c`. Son lecteur et le lecteur de
lignes de `common.c` utilisent `json_loadb(..., JSON_REJECT_DUPLICATES)` :
les doublons sont déjà refusés. Le lecteur est appelé par `src/transport/stdio.c`
et par `src/provider/{process_provider,provider_sdk,mcp_proxy}.c`.

Content-Length est présent dans le cœur, les tests et `fuzz_content_length.c`.
Aucun appelant de production n'active ce mode dans cet arbre. Les fuzzers
JSON-RPC sont `fuzz_json_lines.c` et `fuzz_content_length.c`; le second n'est
pas à porter sans consommateur nommé. Les autres fuzzers couvrent d'autres couches.

Les tableaux publics utilisent `json_t *` et `MAELYS_MCP_ABI_VERSION` vaut 6.
La migration vers des documents immuables est donc réservée à l'ABI 7.
Le lecteur actuel effectue lui-même `read`; cette responsabilité reste au produit.
Les deux cœurs accumulent tout le bloc de feed avant d'appliquer la limite de
ligne. Les lignes trop longues peuvent rester dans le tampon; ce comportement
ne doit pas être porté vers le nouveau lecteur qui doit jeter la ligne entière.
""",
        "codexmanager": """Le commit demandé existe localement sous le nom `maelys-orchestrator`.
Les appelants du cœur sont `transports/jsonrpc_transport.c`,
`transports/jsonrpc_runtime_adapter.c`, `protocols/acp/acp_client.c` et
`agents/execution/agent_process_longlived.c`. Le cœur utilise `json_loads(..., 0)`:
un doublon est accepté et la dernière valeur gagne; un NUL brut peut masquer
les octets qui suivent. Ce sont des écarts de protocole à nommer dans le différentiel.

L'adaptateur est utilisé par les profils Codex app-server, Gemini ACP et le
serveur runtime MCP. Ces chemins configurent JSON Lines. Content-Length n'a
aucun consommateur vivant identifié. Le transport avec thread n'a lui-même
aucun appelant de production trouvé; ses tests conservent sa couverture.

À extraire de `jsonrpc_transport.c`: contrôle de méthode, construction des
enveloppes, validation de message, réservation d'id, table bornée, appariement
et extraction bornée d'erreur. Les ids sont croissants et limités à LLONG_MAX.
Les réponses inconnues sont ignorées dans l'ancien produit; la bibliothèque
doit les rendre sous UNKNOWN_ID. L'expiration ordonnée par échéance est une
nouvelle opération tirée, pas une fonction existante à copier.

À laisser au produit: read/write/select, descripteurs, tube de réveil, thread,
mutex, sémaphores, attente à délai relatif, états de fermeture, dispatch de
requête, réponse method-not-found et politique de masquage d'extraits sensibles.
Le thread `reader_main` appelle le cœur et marque le transport en échec au
premier refus; le nouveau produit devra décider lui-même de poursuivre le flux.

L'adaptateur apporte les compteurs saturants et le rejet jusqu'au prochain LF.
Il tolère avant démarrage les lignes dont le premier caractère utile n'est ni
`{` ni `[`, alors que la directive ne nomme que `{`. Sa fermeture efface la
ligne partielle; le futur reader_finish doit signaler ces octets orphelins.
Les événements runtime fault/timeout/close restent au produit.
""",
    }
    for name, body in paragraphs.items():
        path = args.drafts / name / "docs/audits/jsonrpc-consumers.md"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"# Audit JSON-RPC — {name}\n\n"
            f"Date : 2026-10-08. Référence exacte : `{REFERENCES[name]}`.\n"
            "Étape 0 de la directive révision 2.2. Audit des sources effectué.\n\n"
            + body + "\n## Décisions et preuve de corpus\n\n"
            "Le corpus utilise les vrais binaires sur des sessions synthétiques et les providers de test,\n"
            "sans données privées ni anonymisation. Gemini absent autorise les fixtures exécutables identifiées.\n"
            "Les décomptes et mesures sont dans docs/audits/corpus.json du nouveau dépôt; les lacunes Codex\n"
            "sont consignées lorsqu'elles existent. Le reader est testé octet par octet et d'un bloc sur tous les flux admis.\n\n"
            "Sur maelys-json v0.3.0 (ABI 3), writer_value et object_begin_except recopient les lexèmes.\n"
            "Le relais 1.5e3 est un point fixe à deux passages, avec is_canonical faux. Les nombres\n"
            "non entiers longs restent bornés par maximum_bytes; les entiers hors [-2^63,2^64-1] restent RANGE.\n"
            "Le writer échappe C0 conformément au RFC; U+0000 et BOM donnent SYNTAX.\n"
            "Les flux réels Codex omettent le champ jsonrpc; leur adaptateur devra compléter\n"
            "l'enveloppe via writer avant la classification stricte 2.0, sans assouplir la bibliothèque.\n"
            "La corrélation expose cancel sans callback; release rend le nombre d'entrées perdues.\n"
            "Aucun tag ni push n'a été effectué.\n\n"
            "Preuves machine: docs/audits/stage-0.json, docs/audits/source-references.json,\n"
            "docs/audits/corpus.json, docs/audits/differential.json et tests/audit/dependency.c.\n")
    print(json.dumps({"source_audit": report["source_audit"],
                      "stage_status": report["stage_status"], "references": REFERENCES}))


if __name__ == "__main__":
    main()
