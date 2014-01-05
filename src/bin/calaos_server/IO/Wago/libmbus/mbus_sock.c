/*
 * mbus_sock.c - socket manipulation routines
 *
 * Copyright (c) 2003, Victor Antonovich (avmlink@vlink.ru)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: mbus_sock.c,v 1.1.1.1 2003/09/16 08:33:37 kapyar Exp $
 */

#include "mbus.h"

/*On MacosX MSG_NOSIGNAL is not defined, redefine it here, and use the setsockopt(SO_SIGPIPE) when initializing the socket */
#if !defined(MSG_NOSIGNAL)
#  define MSG_NOSIGNAL 0
#endif

int mbus_sock_create(int blkmode);
int mbus_sock_create_client(const char *server_addr,
                            mbus_uword server_port, int blkmode);
int mbus_sock_select(int sd, int timeout, int wrmode);
int mbus_sock_read(int sd, mbus_ubyte *buf, int len, int timeout);
int mbus_sock_write(int sd, mbus_ubyte *buf, int len, int timeout);

/*
 * Description:
 *   Create new IP socket
 * Parameters:
 *   blkmode - socket mode (nonblocking if non-zero);
 * Return:
 *   Socket descriptor, or -1 in case of error.
 */
int
mbus_sock_create(int blkmode)
{
  int sock, flags;

  /* create socket */
  if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
  {
    DBG(__FILE__, __LINE__,
        "socket(): unable to create socket");
    return -1;
  }

  /* set socket to desired blocking mode */
  if ((flags = fcntl(sock, F_GETFL)) == -1)
  {
    DBG(__FILE__, __LINE__,
                "fcntl(): unable to get flags");
    close(sock);
    return -1;
  }
  flags = blkmode ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
  if ((flags = fcntl(sock, F_SETFL, flags)) == -1)
  {
    DBG(__FILE__, __LINE__,
                "fcntl(): unable to set flags");
    close(sock);
    return -1;
  }

#if defined(SO_NOSIGPIPE)
  // Mac OS X does not have the MSG_NOSIGNAL flag when calling sendo, but we can use this socket option instead
  if (sock > 0)
  {
      int set_option = 1;
      if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &set_option,
                     sizeof(set_option)))
      {
          DBG(__FILE__, __LINE__,
              "setsockopt error");
          return -1;
      }
  }
#endif  // SO_NOSIGPIPE


  /* all OK, return socket descriptor */
  return sock;
}

/*
 * Description:
 *   Create new IP client socket
 * Parameters:
 *   server_port - IP port (0-65535)
 *   server_addr - IP address (DNS or canonical form)
 *   blkmode - socket mode (nonblocking if non-zero);)
 * Return:
 *   Socket descriptor, or -1 in case of error.
 */
int
mbus_sock_create_client(const char *server_addr,
                        mbus_uword server_port, int blkmode)
{
  struct sockaddr_in client_sockaddr;
  struct hostent *server_host;
  static struct in_addr srvaddr;
  int sock_opt = 1;
  int client_s;

  /* create socket in desired blocking mode */
  client_s = mbus_sock_create(blkmode);
  if (client_s < 0) return client_s;

#ifndef NO_COE
  /* set to close socket on exec() */
  if (fcntl(client_s, F_SETFD, 1) < 0)
  {
    DBG(__FILE__, __LINE__,
                "fcntl(): can't set close-on-exec flag");
    close(client_s);
    return -1;
  }
#endif

  /* set reuse socket address flag */
  if ((setsockopt(client_s, SOL_SOCKET, SO_REUSEADDR,
                  (void *)&sock_opt, sizeof(sock_opt))) < 0)
  {
    DBG(__FILE__, __LINE__,
        "setsockopt(): can't set SO_REUSEADDR flag");
    close(client_s);
    return -1;
  }

  memset(&client_sockaddr, 0, sizeof(client_sockaddr));

  client_sockaddr.sin_family = AF_INET;
  client_sockaddr.sin_port = htons(server_port);

  srvaddr.s_addr = inet_addr(server_addr);
  if (srvaddr.s_addr == INADDR_NONE)
  {
    server_host = gethostbyname(server_addr);
    if (server_host != NULL)
    {
      if ((unsigned)server_host->h_length >
            sizeof(client_sockaddr.sin_addr) ||
          server_host->h_length < 0)
      {
        DBG(__FILE__, __LINE__,
            "gethostbyname(): illegal address");
        close(client_s);
        return -1;
      }
      memcpy(&client_sockaddr.sin_addr,
             server_host->h_addr_list[0],
             sizeof(client_sockaddr.sin_addr));
    }
    else
    {
      DBG(__FILE__, __LINE__,
                  "gethostbyname(): can't understand specified address");
      close(client_s);
      return -1;
    }
  }
  else
    memcpy(&client_sockaddr.sin_addr,
           &srvaddr.s_addr, sizeof(srvaddr.s_addr));

  /* let's connect */
  if (connect(client_s,
              (struct sockaddr *)&client_sockaddr,
              sizeof(client_sockaddr)) < 0)
  {
    DBG(__FILE__, __LINE__, \
        "connect(): unable to connect to specified address");
    close(client_s);
    return -1;
  }
  /* successfully connected */
  return client_s;
}

/*
 * Description:
 *   Wait for socket descriptor to be readable
 * Parameters:
 *   sd - socket descriptor;
 *   timeout - the timeout in seconds;
 *   wrmode - if is non-zero, checks for sd being writable instead
 * Return:
 *   1 if sd is accessible, 0 if timeout and -1 if error in select().
 */
int
mbus_sock_select(int sd, int timeout, int wrmode)
{
  fd_set fds, exceptfds;
  struct timeval t_out;

  FD_ZERO(&fds);
  FD_SET(sd, &fds);
  FD_ZERO(&exceptfds);
  FD_SET(sd, &exceptfds);
  t_out.tv_sec = timeout;
  t_out.tv_usec = 0;
  return select(sd + 1,
                wrmode ? NULL : &fds, wrmode ? &fds : NULL,
                &exceptfds, &t_out);
}

/*
 * Description:
 *  Read data from socket to buffer with timeout
 * Parameters:
 *   sd - socket descriptor;
 *   len - number of bytes to read;
 *   buf - pointer to the data buffer;
 *   timeout - the timeout in seconds
 * Return:
 *   Number of successfully readed bytes or
 *   -1 if error caused.
 */
int
mbus_sock_read(int sd, mbus_ubyte *buf, int len, int timeout)
{
  int res = 0, rd_len = 0;
  while (rd_len < len)
  {
    do
    {
      if (timeout)
      {
        do
        {
          res = mbus_sock_select(sd, timeout, 0);
        } while (res == -1 && errno == EINTR);
        if (res <= 0) break;
      }
      /*res = read(sd, buf, len - rd_len);*/
      res = recv(sd, buf, len - rd_len, MSG_NOSIGNAL);
    } while (res == -1 && errno == EINTR);
    if (res <= 0) break;
    buf += res;
    rd_len += res;
  }
  return (res >= 0) ? rd_len : -1;
}

/*
 * Description:
 *  Write data from buffer to socket with timeout
 * Parameters:
 *   sd - socket descriptor;
 *   len - number of bytes to read;
 *   buf - pointer to the data buffer;
 *   timeout - the timeout in seconds
 * Return:
 *   Number of successfully written bytes or
 *   -1 if error caused.
 */
int
mbus_sock_write(int sd, mbus_ubyte *buf, int len, int timeout)
{
  int res = 0, wr_len = 0;
  while (wr_len < len)
  {
    do
    {
      if (timeout)
      {
        do
        {
          res = mbus_sock_select(sd, timeout, 1);
        } while (res == -1 && errno == EINTR);
        if (res <= 0) break;
      }
      /*res = write(sd, buf, len - wr_len);*/
      res = send(sd, buf, len - wr_len, MSG_NOSIGNAL);
    } while (res == -1 && errno == EINTR);
    if (res <= 0) break;
    buf += res;
    wr_len += res;
  }
  return (res >= 0) ? wr_len : -1;
}
