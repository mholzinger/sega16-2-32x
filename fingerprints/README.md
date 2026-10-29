# Test-cartridge fingerprints

One file per machine, from `make line WRAMCODE=1 TESTCART=1`. The suite reports
what a machine DOES; the finding is a diff between files, not a pass count.
`docs/design/TESTCART.md` says why.

Real 32X hardware is the reference where we have it. The MiSTer core is an
implementation under test, not an oracle.
