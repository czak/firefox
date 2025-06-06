import argparse
import csv
import json
import struct
import subprocess
import sys
from datetime import datetime
from io import StringIO

import requests
from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization

# Format: CCADB Record Creation Date, SHA-256 Fingerprint, Subject Key Identifier, Authority Key Identifier, Root or Intermediate Certificate Record, X.509 Certificate PEM
REPORT_URL = "https://ccadb.my.salesforce-sites.com/ccadb/WebTrustListAsOf?ListDate={}"
DATE_ADDITION_COL = "CCADB Record Creation Date"
CERT_PEM_COL = "X.509 Certificate PEM"


class IdentifierAllocator:
    # Prefix should be a byte string.p
    def __init__(self, prefix):
        self.prefix = prefix
        self.position = 0

    def getIdentifier(self):
        result = self.prefix + struct.pack(">H", self.position)
        self.position += 1
        return result


def get_webtrust_certs(list_date):
    output = []
    url = REPORT_URL.format(list_date)
    response = requests.get(url, timeout=10)
    response.raise_for_status()
    csv_data = response.text
    with open(f"webtrust_certs-{list_date}.csv", "w") as certs_file:
        certs_file.write(csv_data)
    for r in csv.DictReader(StringIO(csv_data)):
        timestamp = datetime.strptime(r[DATE_ADDITION_COL], "%Y-%m-%dT%H:%M:%SZ")
        cert = x509.load_pem_x509_certificate(
            r[CERT_PEM_COL].encode("ascii"), default_backend()
        )
        certDer = cert.public_bytes(serialization.Encoding.DER)
        output.append((timestamp, certDer))
    print(f"Loaded {len(output)} certs from {url}")
    return output


def create_cert_dict(certs):
    certs.sort(key=lambda x: x[0])
    output = dict()
    idAlloc = IdentifierAllocator(b"\xff")
    for _, der in certs:
        idHex = idAlloc.getIdentifier()
        output[idHex] = der
    return output


def load_json_cache(jf):
    with open(jf) as f:
        j = json.load(f)
    assert j["data"]
    assert j["list_date"]
    assert j["creation_date"]
    j["data"] = {bytes.fromhex(k): bytes.fromhex(v) for k, v in j["data"].items()}
    return j


def cert_to_hash_rust_array(cert):
    digest = hashes.Hash(hashes.SHA256(), backend=default_backend())
    digest.update(cert)

    # Finalize the hash and get the digest
    sha256_hash = digest.finalize()

    # Format the hash as a Rust array
    rust_array = ", ".join(f"0x{byte:02x}" for byte in sha256_hash)
    rust_output = f"[{rust_array}]"
    return rust_output


def make_rust_file_contents(certs, generation_date, list_date):
    output = ""
    output += f"""
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use log;
use std::sync::OnceLock;
use thin_vec::ThinVec;

// Autogenerated via security/manager/ssl/abridged_certs/tools/generate_builtins.py
// Generation Date: {generation_date}
// Based on list version: {list_date}
"""
    output += """

// Public Interface

/// Given an Abridged Cert Identifier, lookup the hash of the corresponding certificate
pub fn id_to_hash(id: &[u8; 3]) -> Option<&ThinVec<u8>> {
    let index: usize = u16::from_be_bytes([id[1], id[2]]).into();
    log::trace!("Parsed identifier {:#02X?} as index {}", id, index);
    ABRIDGED_CERTS_BUILTINS_HASHES.get_or_init(init_hashes).get(index)
}

/// Get a list of hashes needed for this Abridged Certs scheme
pub fn get_needed_hashes() -> Option<&'static ThinVec<ThinVec<u8>>> {
    Some(ABRIDGED_CERTS_BUILTINS_HASHES.get_or_init(init_hashes))
}

// Private Implementation

/// This is currently built whenever it is accessed (similar a to lazy_static)
/// However, we may want explicit control in the future , e.g. if we want to delay
/// construction until after cert_storage has synced, or if we want to use a manifest
/// from remote settings.
static ABRIDGED_CERTS_BUILTINS_HASHES: OnceLock<ThinVec<ThinVec<u8>>> = OnceLock::new();

/// rustc / LLVM has a number of outstanding bugs in its code generation for large
/// functions. See the discussion in Bug 1969383 for why this format was selected.
"""
    output += f"const ABRIDGED_CERT_BYTES: [u8; {len(certs) * 32}] = ["
    for id, cert in certs.items():
        output += cert_to_hash_rust_array(cert).strip("[]") + ", "
    output += "];\n"

    output += """pub fn init_hashes() -> ThinVec<ThinVec<u8>>{"""
    output += f"""let mut m = ThinVec::with_capacity({len(certs)});"""
    output += """
            for entry in ABRIDGED_CERT_BYTES.chunks(32) {
                m.push(ThinVec::from(entry));
            }
            m.shrink_to_fit();
            m
        }
"""

    return output


if __name__ == "__main__":
    today = datetime.now().strftime("%Y-%m-%d")
    parser = argparse.ArgumentParser(
        description="Builds a map from identifiers to WebPKI Intermediate and Root Certificates"
    )
    parser.add_argument(
        "-d",
        "--date",
        help="Specify the date you want the list as-of (YYYY-MM-DD format)",
        type=str,
        default=today,
    )
    parser.add_argument(
        "-o",
        "--output",
        help="Specify the output file path",
        type=str,
        default="builtins.rs",
    )
    parser.add_argument(
        "-i",
        "--input",
        help="Specify a cached version of the list from JSON. Overrides date option.",
        type=str,
        default=None,
    )
    args = parser.parse_args()

    certs = None
    if args.input:
        j = load_json_cache(args.input)
        certs = j["data"]
        args.date = j["list_date"]
        today = j["creation_date"]
    else:
        certs = get_webtrust_certs(args.date)
        print(f"Fetched {len(certs)} certificates")
        certs = create_cert_dict(certs)

    with open(args.output, "w") as rust_file:
        rust_file.write(make_rust_file_contents(certs, today, args.date))

    subprocess.run(["rustfmt", args.output], capture_output=True, text=True, check=True)

    print(f"Generated file output to {args.output}")
    sys.exit(0)
