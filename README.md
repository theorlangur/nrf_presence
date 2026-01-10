MCUBoot serial recovery:

speed issue:
changed ~/myapps/go/mynewt-mcumgr-cli/modified_mods/newtmgr@v0.0.0-20201028150837-60b2da78788c/nmxact/nmserial/serial_xport.go
specifically changed Sleep from 20ms to 1ms (there's one such sleep)
this upped transfer rate from ~ 830 bytes/s to ~ 4.5kb/s

speed issue update:
using mcumgr-client (rust-based tool), changed mtu to 4k and line input size to 8k
tweaked amount of retries + subsequent timeout duration (16 retries, 1000ms timeout)
and I'm getting an ok speed of ~ 6kb/s
