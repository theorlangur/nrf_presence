MCUBoot serial recovery:

speed issue:
changed ~/myapps/go/mynewt-mcumgr-cli/modified_mods/newtmgr@v0.0.0-20201028150837-60b2da78788c/nmxact/nmserial/serial_xport.go
specifically changed Sleep from 20ms to 1ms (there's one such sleep)
this upped transfer rate from ~830 bytes/s to ~4.5kb/s
