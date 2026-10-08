"""Replace IAT pointer labels and Winsock ordinals in Ghidra output with real names."""
import re, sys
WS2 = {1: "accept", 2: "bind", 3: "closesocket", 4: "connect", 5: "getpeername", 6: "getsockname",
       7: "getsockopt", 8: "htonl", 9: "htons", 10: "ioctlsocket", 11: "inet_addr", 12: "inet_ntoa",
       13: "listen", 14: "ntohl", 15: "ntohs", 16: "recv", 17: "recvfrom", 18: "select", 19: "send",
       20: "sendto", 21: "setsockopt", 22: "shutdown", 23: "socket", 51: "gethostbyaddr",
       52: "gethostbyname", 57: "gethostname", 111: "WSAGetLastError", 115: "WSAStartup", 116: "WSACleanup"}
src, tsv = sys.argv[1], sys.argv[2]
names = {}
for line in open(tsv):
    ptr, dll, name = line.rstrip("\n").split("\t")
    names[ptr] = name
text = open(src).read()
text = re.sub(r"PTR_[0-9a-f]{8}", lambda m: names.get(m.group(0), m.group(0)), text)
text = re.sub(r"Ordinal_(\d+)", lambda m: WS2.get(int(m.group(1)), m.group(0)), text)
sys.stdout.write(text)
