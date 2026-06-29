# Security Policy

## Supported versions

Until `egl-util-cpp` reaches `v1.0.0`, only the latest released minor line receives
security fixes. After `1.0.0`, the supported window is defined in
[`docs/workflow/maintenance.md`](docs/workflow/maintenance.md).

| Version | Supported |
|---------|-----------|
| latest released `0.x` | ✅ |
| older `0.x` | ❌ |

## Reporting a vulnerability

**Do not open a public issue or PR for a security problem.** Report it privately via
[GitHub private vulnerability reporting](https://docs.github.com/code-security/security-advisories)
on this repository (**Security** tab → *Report a vulnerability*), to `danielPoloWork`.

Please include:

- the affected version(s) and platform/toolchain;
- a minimal reproduction (a failing test is ideal);
- the observed impact and, if known, the root cause.

## What to expect

1. **Acknowledgement** of the report.
2. **Triage & fix under embargo** on a private branch / draft advisory; the SemVer level of
   the fix is assessed by the decision tree in
   [`docs/workflow/maintenance.md`](docs/workflow/maintenance.md).
3. **Coordinated release**: the fix ships, then the advisory is published. The fix is
   recorded in `CHANGELOG.md` under a **Security** entry with the advisory / CVE reference.
4. **Backport** to every still-supported release line.

Thank you for reporting responsibly.
