# Zeemu Virtual Filesystem

This directory is the writable host-side backing store for Zeebo `fs:/` paths
that are not already mapped to extracted ROM assets.

Initial default mappings:

```text
fs:/shared/ -> vfs/shared/
fs:/sys/    -> vfs/sys/
fs:/lctsys/ -> vfs/lctsys/
```

The emulator code should access these through `VirtualFileSystem`, not by using
host paths directly.
