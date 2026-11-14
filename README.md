# WinIO: a socket operation that finishes at once still queues a completion packet

## Running it

**The crash.** In `crash/`, on Windows, with a GHC whose WinIO support is enabled for libraries (`__IO_MANAGER_WINIO__ >= 2`: 9.14.2, currently [9.14.2-rc3](https://downloads.haskell.org/~ghc/9.14.2-rc3/); 9.14.1 and earlier compile network's WinIO code out) and cabal:

```
cabal run -w <path to 9.14.2 ghc.exe> --project-file=cabal.project.original crash    # network master
cabal run -w <path to 9.14.2 ghc.exe> --project-file=cabal.project.fixed crash       # master plus the fix
```

The project files build network from [codedownio/network](https://github.com/codedownio/network) commits with the generated `configure` script committed (branches `repro-original` and `repro-skip-fixed`); running `configure` needs an `sh`, which ghcup's msys2 or Git for Windows provides. `crash` runs 64 busy loopback socket pairs under `--io-manager=native` with the GC reusing memory. Results on Windows 11 with 9.14.2-rc3:

```
original: crashes at once, every run (exit code 11)
  Access violation in generated code when reading 0xffffffffffffffff
fixed:    survived 120s, every run
```

**The stale packets.** In the top directory, with any GHC, in PowerShell:

```
.\run.ps1 original    # sockets associated with the completion port as network master does
.\run.ps1 fixed       # also with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS, as the fix does
```

```
without (network master)                 WSARecv: 100/100 finished at once, 100 packets queued | WSASend: 100/100 at once, 100 packets queued
with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS WSARecv: 100/100 finished at once,   0 packets queued | WSASend: 100/100 at once,   0 packets queued
```

## The problem

Under GHC's native I/O manager (WinIO), network associates sockets with the I/O manager's completion port, and Windows then queues a completion packet for every `WSARecv`/`WSASend`, even one that finishes immediately. But when a request finishes immediately, GHC completes it inline and frees its bookkeeping record (the `HASKELL_OVERLAPPED`). The packet Windows queued for it is still on the port, and when the I/O manager later dequeues it, it reads that freed record. Once the memory has been reused, it follows garbage and the process crashes.

## The fix

[codedownio/network `winio-skip-completion-on-success`](https://github.com/codedownio/network/tree/winio-skip-completion-on-success) sets `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` on a socket when it's associated with the completion port, in `socket` and `accept`. With that flag, Windows doesn't queue a packet for an operation that finishes immediately, so there is nothing left to dequeue after GHC has completed and freed the request. Operations that really go pending still get their packet as before. This is the same flag GHC sets on its own file handles, whose inline completion path relies on it. Microsoft [documents the flag for sockets](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setfilecompletionnotificationmodes), with one stated exception: sockets behind a non-IFS Layered Service Provider, a mechanism some older firewalls and proxies used, [deprecated since Windows 8](https://learn.microsoft.com/en-us/windows/win32/winsock/categorizing-layered-service-providers-and-applications).

## Extra information

`sync_completion.c` associates a socket with a completion port the way WinIO does, issues 100 receives and 100 sends that finish at once, and counts the packets that land on the port. `run.ps1` compiles it with the C toolchain GHC ships with (`ghc -no-hs-main`); pass `-Ghc ghc-9.12.4` to use a particular GHC.

The inline completion path: network master answers `Mgr.CbPending` when a call succeeds at once (since `c22363a`). GHC's `withOverlappedEx` (`GHC.Internal.Event.Windows`) then calls `getOverlappedResult` straight away, finds the request finished, swaps the record's completion-data pointer for `nullReq`, runs the callback and returns, after which `withRequest` releases the `HASKELL_OVERLAPPED` (an `allocaBytes` buffer). When `processCompletion` dequeues the stale packet, a record that hasn't been reused yet still holds `nullReq` and the packet is ignored; one that has been reused is followed as if it were live.

The crash needs that reuse to happen before the packet is processed; `crash/` arranges that with many busy sockets and frequent GCs, and it's what our application (warp, websockets, dozens of busy sockets) hit too: the server died within a minute in `GHC.Internal.Event.Windows.$wprocessCompletion`, and runs clean with the fix.
