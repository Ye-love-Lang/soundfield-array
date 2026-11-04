---
name: verified-fpga-release
description: Prepare and audit FPGA releases using file manifests, Vivado report extraction, and evidence-scoped deployment records. Use for release verification or interpreting measured FPGA results, not for assuming hardware or acoustic acceptance.
---

# Verified FPGA release

Read [workflow](references/workflow.md) before selecting an operation. Work from the user's actual artifact root; do not assume this project's board, IP address or register map on another design.

- Run scripts/verify_manifest.py against a manifest with path, bytes, sha256 entries. Missing, modified or root-escaping files fail. Hash agreement establishes file identity only.
- Run scripts/analyze_reports.py with a Vivado utilization and timing report. A missing metric fails rather than defaulting to zero. Positive slack covers declared constraints only.
- Use [prompts](references/prompts.md) to request a partition proposal, diagnose a failed capture, or summarize a release. Keep model proposals distinct from tool output and hardware evidence.
- Use [failure cases](references/failure_cases.md) when a capture is rejected, a new filter design is invalid, or JTAG and cold boot are being confused.

Never flash or publish as a side effect of these read-only audit tools. Use the user's authorized deployment flow if deployment is requested. Preserve a usable release before changing it. Record coding, compilation, deployment and measured acceptance separately.
