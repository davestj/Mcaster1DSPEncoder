# Mcaster1DSPEncoder — GnuPG Binary & Package Signing

**Maintainer:** Dave St. John <davestj@mcaster1.com>
**Last Updated:** 2026-03-27

---

## Signing Keys

### Binary Code Signing

Used for signing compiled binaries (`mcaster1-dsp-encoder-admin`, `mcaster1-dsp-encoder`, `mcaster1-voictune`).

```
Key ID:      6C07628DF4D94C20
Fingerprint: 35CCA6D31103C4EA354C0FCC6C07628DF4D94C20
UID:         David St John (MCaster1 Binary Code Signing) <davestj@mcaster1.com>
Algorithm:   RSA 4096-bit
Created:     2026-03-27
Expires:     2029-03-26
```

### Package Signing

Used for signing release tarballs, `.deb` packages, and repository metadata.

```
Key ID:      A29A09463F34D8D5
Fingerprint: 6951A77CEF64600EE17FD96AA29A09463F34D8D5
UID:         David St John (MCaster1 LLC Package Signing) <davestj@mcaster1.com>
Algorithm:   RSA 4096-bit
Created:     2026-03-10
```

---

## How Signing Works

### Binary Signing

After compilation, each binary gets a detached GPG signature (`.sig` file):

```bash
# Sign all binaries
bash scripts/sign-binaries.sh

# Manual signing
gpg --local-user 6C07628DF4D94C20 --armor --detach-sign src/linux/mcaster1-voictune
```

This creates `mcaster1-voictune.asc` alongside the binary.

### Verification

```bash
# Verify a binary signature
gpg --verify src/linux/mcaster1-voictune.asc src/linux/mcaster1-voictune

# Expected output:
# gpg: Good signature from "David St John (MCaster1 Binary Code Signing) <davestj@mcaster1.com>"
```

### Package Signing

```bash
# Sign a release tarball
gpg --local-user A29A09463F34D8D5 --armor --detach-sign mcaster1-dsp-encoder-1.8.0-beta.1.tar.gz
```

---

## Exporting Public Keys

For distribution to users who want to verify signatures:

```bash
# Export binary signing public key
gpg --armor --export 6C07628DF4D94C20 > docs/keys/mcaster1-binary-signing.asc

# Export package signing public key
gpg --armor --export A29A09463F34D8D5 > docs/keys/mcaster1-package-signing.asc
```

---

## SHA256 Checksums

The signing script also generates SHA256 checksums:

```bash
sha256sum src/linux/mcaster1-dsp-encoder-admin \
          src/linux/mcaster1-dsp-encoder \
          src/linux/mcaster1-voictune > checksums-v1.8.0-beta.1.sha256
```

---

## Key Management

- Private keys are stored in `~/.gnupg/` (never committed to git)
- Public keys are exported to `docs/keys/` for distribution
- Revocation certificates stored in `~/.gnupg/openpgp-revocs.d/`
- Binary signing key expires 2029-03-26 — renew before expiry
