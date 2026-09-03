#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys
from typing import Any
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener


PYTHON_ROOT = Path(__file__).resolve().parents[1]

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from ptx_frontend.code_gen.load_yaml import load_yaml


class BaselineVerificationError(ValueError):
    pass


class RejectRedirects(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        raise BaselineVerificationError(f"unexpected redirect to {new_url}")


def required_string(mapping: dict[str, Any], key: str) -> str:
    value = mapping.get(key)
    if not isinstance(value, str) or not value:
        raise BaselineVerificationError(f"missing or invalid {key}")
    return value


def validate_subject(subject: str, toolkit_release: str) -> None:
    parsed = urlsplit(subject)
    archive_path = f"/cuda/archive/{toolkit_release}/parallel-thread-execution/"
    if (
        parsed.scheme != "https"
        or parsed.netloc != "docs.nvidia.com"
        or parsed.query
        or parsed.fragment
        or not parsed.path.startswith(archive_path)
    ):
        raise BaselineVerificationError(f"invalid archive subject {subject}")


def subject_from_baseline(baseline: dict[str, Any]) -> tuple[str, str]:
    digest = baseline.get("content_digest")
    sources = baseline.get("sources")
    if not isinstance(digest, dict) or not isinstance(sources, dict):
        raise BaselineVerificationError("missing digest or sources mapping")

    subject = required_string(digest, "subject")
    expected_digest = required_string(digest, "value")
    if digest.get("algorithm") != "sha256":
        raise BaselineVerificationError("unsupported digest algorithm")
    if digest.get("canonicalization") != "raw_response_body":
        raise BaselineVerificationError("unsupported digest canonicalization")
    if subject != required_string(sources, "contents"):
        raise BaselineVerificationError("digest subject must equal sources.contents")

    validate_subject(subject, required_string(baseline, "toolkit_release"))
    return subject, expected_digest


def sha256_hex(body: bytes) -> str:
    return hashlib.sha256(body).hexdigest()


def verify_digest(body: bytes, expected_digest: str) -> str:
    actual_digest = sha256_hex(body)
    if actual_digest != expected_digest:
        raise BaselineVerificationError(
            f"digest mismatch: expected {expected_digest}, got {actual_digest}"
        )
    return actual_digest


def fetch_raw_response_body(subject: str, opener=None) -> bytes:
    active_opener = opener or build_opener(RejectRedirects())
    request = Request(subject, headers={"Accept-Encoding": "identity"})
    with active_opener.open(request, timeout=30) as response:
        if response.geturl() != subject:
            raise BaselineVerificationError(
                f"unexpected redirect to {response.geturl()}"
            )
        if response.headers.get("Content-Encoding") is not None:
            raise BaselineVerificationError("response must not have Content-Encoding")
        body = response.read()
    if not isinstance(body, bytes):
        raise BaselineVerificationError("response body is not bytes")
    return body


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify the archived PTX ISA baseline response digest."
    )
    parser.add_argument("baseline", type=Path, help="Path to the baseline YAML file.")
    return parser.parse_args()


def main() -> None:
    args = parse_arguments()
    subject, expected_digest = subject_from_baseline(load_yaml(args.baseline))
    verify_digest(fetch_raw_response_body(subject), expected_digest)
    print(f"{args.baseline}: digest OK")


if __name__ == "__main__":
    main()
