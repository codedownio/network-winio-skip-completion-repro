# WinIO: a socket operation that finishes at once still queues a completion packet

## Running it

On Windows, with GHC (e.g. from [ghcup](https://www.haskell.org/ghcup/)), in PowerShell:

```
.\run.ps1 original    # sockets associated with the completion port as network master does
.\run.ps1 fixed       # also with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS, as the fix does
```

`run.ps1` compiles `sync_completion.c` with the C toolchain GHC ships with (`ghc -no-hs-main`) and runs it; pass `-Ghc ghc-9.12.4` to use a particular GHC. Results on Windows 11:

```
without (network master)                 WSARecv: 100/100 finished at once, 100 packets queued | WSASend: 100/100 at once, 100 packets queued
with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS WSARecv: 100/100 finished at once,   0 packets queued | WSASend: 100/100 at once,   0 packets queued
```

The fix is [codedownio/network `winio-skip-completion-on-success`](https://github.com/codedownio/network/tree/winio-skip-completion-on-success).

## What it shows

Under GHC's native I/O manager (WinIO), network associates sockets with the I/O manager's completion port but doesn't set `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` on them. Without that flag, Windows queues a completion packet even for a `WSARecv`/`WSASend` that finishes immediately. `sync_completion.c` associates a socket with a completion port the way WinIO does, issues 100 receives and 100 sends that finish at once, and counts the packets that land on the port.

## Why it matters

network master answers `Mgr.CbPending` when a call succeeds at once (since `c22363a`). GHC's `withOverlappedEx` (`GHC.Internal.Event.Windows`) then calls `getOverlappedResult` straight away, finds the request finished, and completes it inline: it swaps the record's completion-data pointer for `nullReq`, runs the callback, and returns, after which `withRequest` releases the `HASKELL_OVERLAPPED` (an `allocaBytes` buffer). The packet Windows queued for that request is still on the port. When the I/O manager dequeues it, `processCompletion` reads the record through the packet's `lpOverlapped`:

- if the memory hasn't been reused yet, it still holds `nullReq` and the packet is ignored;
- if it has, `processCompletion` follows whatever is there.

So every socket operation that finishes immediately leaves a packet pointing at freed memory. It's harmless until the memory is reused before the packet is processed, which needs load: in our application's test suite (dozens of busy sockets, a web server, websockets) the server died within a minute with

```
Access violation in generated code when reading 0xffffffffffffffff
  * 0x14874d686  codedown-server.exe+0x874d686
```

which resolves to `GHC.Internal.Event.Windows.$wprocessCompletion`. A small stress program (64 socket pairs, GC after every receive) didn't hit the window in 11 one-minute runs, so this repro demonstrates the packet rather than the crash.

With `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` set when a socket is associated (as GHC's own file handles get), no packet is queued for an operation that finishes at once, and the suite passes.
