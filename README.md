# WinIO: a socket operation that finishes at once still queues a completion packet

## Running it

On Windows, with GHC (e.g. from [ghcup](https://www.haskell.org/ghcup/)), in PowerShell:

```
.\run.ps1 original    # sockets associated with the completion port as network master does
.\run.ps1 fixed       # also with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS, as the fix does
```

Results on Windows 11:

```
socket provider is IFS (XP1_IFS_HANDLES): yes
without (network master)                 WSARecv: 100/100 finished at once, 100 packets queued | WSASend: 100/100 at once, 100 packets queued
with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS WSARecv: 100/100 finished at once,   0 packets queued | WSASend: 100/100 at once,   0 packets queued
```

## The problem

Under GHC's native I/O manager (WinIO), network associates sockets with the I/O manager's completion port, and Windows then queues a completion packet for every `WSARecv`/`WSASend`, even one that finishes immediately. But when a request finishes immediately, GHC completes it inline and frees its bookkeeping record (the `HASKELL_OVERLAPPED`). The packet Windows queued for it is still on the port, and when the I/O manager later dequeues it, it reads that freed record. Once the memory has been reused, it follows garbage and the process crashes.

## The fix

[codedownio/network `winio-skip-completion-on-success`](https://github.com/codedownio/network/tree/winio-skip-completion-on-success) sets `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` on a socket when it's associated with the completion port, in `socket` and `accept`. With that flag, Windows doesn't queue a packet for an operation that finishes immediately, so there is nothing left to dequeue after GHC has completed and freed the request. Operations that really go pending still get their packet as before. This is the same flag GHC sets on its own file handles, whose inline completion path relies on it. The flag works on ordinary sockets, which go straight to Windows' built-in TCP/IP provider; the one documented exception is a socket behind a non-IFS Layered Service Provider (a deprecated mechanism some older firewalls and proxies used), so the fix checks the socket's `XP1_IFS_HANDLES` bit first and leaves such a socket alone. The repro prints that bit for a plain loopback socket: it's set.

## Extra information

`sync_completion.c` associates a socket with a completion port the way WinIO does, issues 100 receives and 100 sends that finish at once, and counts the packets that land on the port. `run.ps1` compiles it with the C toolchain GHC ships with (`ghc -no-hs-main`); pass `-Ghc ghc-9.12.4` to use a particular GHC.

The inline completion path: network master answers `Mgr.CbPending` when a call succeeds at once (since `c22363a`). GHC's `withOverlappedEx` (`GHC.Internal.Event.Windows`) then calls `getOverlappedResult` straight away, finds the request finished, swaps the record's completion-data pointer for `nullReq`, runs the callback and returns, after which `withRequest` releases the `HASKELL_OVERLAPPED` (an `allocaBytes` buffer). When `processCompletion` dequeues the stale packet, a record that hasn't been reused yet still holds `nullReq` and the packet is ignored; one that has been reused is followed as if it were live.

The crash needs that reuse to happen before the packet is processed, so it shows up under load. In our application's test suite (dozens of busy sockets, a web server, websockets) the server died within a minute with

```
Access violation in generated code when reading 0xffffffffffffffff
  * 0x14874d686  codedown-server.exe+0x874d686
```

which resolves to `GHC.Internal.Event.Windows.$wprocessCompletion`, and passes with the fix. A small stress program (64 socket pairs, GC after every receive) didn't hit the window in 11 one-minute runs, so this repro demonstrates the stale packets rather than the crash.
