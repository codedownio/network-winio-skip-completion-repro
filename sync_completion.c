/* Does a WSARecv/WSASend that completes at once still queue a completion packet?
   Associates a socket with a completion port the way GHC's WinIO does, issues operations that
   finish immediately, and checks the port.
     sync_completion original   socket associated as network master does
     sync_completion fixed      also with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void pair(SOCKET *a, SOCKET *b) {
  struct sockaddr_in addr = {0}; int len = sizeof addr;
  addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  SOCKET l = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
  bind(l, (struct sockaddr *)&addr, sizeof addr); listen(l, 1);
  getsockname(l, (struct sockaddr *)&addr, &len);
  *a = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
  connect(*a, (struct sockaddr *)&addr, sizeof addr);
  *b = accept(l, NULL, NULL);
  closesocket(l);
}

static void trial(int skip) {
  SOCKET client, server; pair(&client, &server);
  WSAPROTOCOL_INFOW info; int infoLen = sizeof info;
  int ifs = getsockopt(server, SOL_SOCKET, SO_PROTOCOL_INFOW, (char *)&info, &infoLen) == 0
            && (info.dwServiceFlags1 & XP1_IFS_HANDLES);
  printf("socket provider is IFS (XP1_IFS_HANDLES): %s\n", ifs ? "yes" : "no");
  HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  CreateIoCompletionPort((HANDLE)server, port, 42, 0);
  if (skip) SetFileCompletionNotificationModes((HANDLE)server, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);

  int recvQueued = 0, sendQueued = 0, recvSync = 0, sendSync = 0;
  for (int i = 0; i < 100; i++) {
    char buf[16]; WSABUF b = { sizeof buf, buf }; DWORD n = 0, flags = 0;
    DWORD bytes; ULONG_PTR key; LPOVERLAPPED got;

    send(client, "x", 1, 0); Sleep(1);                 /* data is waiting */
    OVERLAPPED ov = {0};
    if (WSARecv(server, &b, 1, &n, &flags, &ov, NULL) == 0) recvSync++;
    if (GetQueuedCompletionStatus(port, &bytes, &key, &got, 50) && got == &ov) recvQueued++;

    OVERLAPPED ov2 = {0}; WSABUF s = { 1, "y" };
    if (WSASend(server, &s, 1, &n, 0, &ov2, NULL) == 0) sendSync++;
    if (GetQueuedCompletionStatus(port, &bytes, &key, &got, 50) && got == &ov2) sendQueued++;
    recv(client, buf, sizeof buf, 0);
  }
  printf("%-40s WSARecv: %3d/100 finished at once, %3d packets queued | WSASend: %3d/100 at once, %3d packets queued\n",
         skip ? "with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS" : "without (network master)",
         recvSync, recvQueued, sendSync, sendQueued);
  closesocket(client); closesocket(server); CloseHandle(port);
}

int main(int argc, char **argv) {
  if (argc != 2 || (strcmp(argv[1], "original") && strcmp(argv[1], "fixed"))) {
    fprintf(stderr, "usage: sync_completion original|fixed\n");
    return 2;
  }
  WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
  trial(strcmp(argv[1], "fixed") == 0);
  return 0;
}
