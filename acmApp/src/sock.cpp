
#include "acm_drv.h"


Socket::Socket(int af, int type, int proto)
    :sock(epicsSocketCreate(af, type, proto))
{
    if(sock==INVALID_SOCKET)
        throw std::runtime_error("Too many sockets");
}

Socket::~Socket()
{
    close();
}

void Socket::close()
{
    if(sock!=INVALID_SOCKET) {
        epicsSocketDestroy(sock);
        sock = INVALID_SOCKET;
    }
}

Socket::ShowAddr::ShowAddr(const osiSockAddr& addr)
{
    ipAddrToDottedIP(&addr.ia, buf, sizeof(buf));
    buf[sizeof (buf)-1] = '\0'; // paranoia
}
