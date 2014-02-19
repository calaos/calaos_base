/*! \file      smtpsocket.h
 *  \brief     header file for TCP/IP socket and SMTP communication functions
 *  \author    Brecht Sanders
 *  \date      2012-2013
 *  \copyright GPL
 */
/*
    This file is part of libquickmail.

    libquickmail is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libquickmail is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libquickmail.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __INCLUDED_SMTPSOCKET_H
#define __INCLUDED_SMTPSOCKET_H

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifndef SOCKET
#define SOCKET int
#endif
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif
#endif
#include <stdio.h>
#include <stdarg.h>

#define READ_BUFFER_CHUNK_SIZE 128
#define WRITE_BUFFER_CHUNK_SIZE 128

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief connect network socket
 * \param  smtpserver      hostname or IP address of server
 * \param  smtpport        TCP port to connect to
 * \param  errmsg          optional pointer to where error message will be stored (must not be freed by caller)
 * \return open network socket or INVALID_SOCKET on error
*/
SOCKET socket_open (const char* smtpserver, unsigned int smtpport, char** errmsg);

/*! \brief disconnect network socket
 * \param  sock        open network socket
*/
void socket_close (SOCKET sock);

/*! \brief send data to a network socket
 * \param  sock        open network socket
 * \param  buf         buffer containing data
 * \param  len         size of buffer in bytes
 * \return number of bytes sent
*/
int socket_send (SOCKET sock, const char* buf, int len);

/*! \brief check if data is waiting to be read from network socket
 * \param  sock            open network socket
 * \param  timeoutseconds  number of seconds to wait (0 to return immediately)
 * \return nonzero if data is waiting
*/
int socket_data_waiting (SOCKET sock, int timeoutseconds);

/*! \brief read SMTP response from network socket
 * \param  sock            open network socket
 * \return null-terminated string containing received data (must be freed by caller), or NULL
*/
char* socket_receive_stmp (SOCKET sock);

/*! \brief read SMTP response from network socket
 * \param  sock            open network socket
 * \param  errmsg          optional pointer to where error message will be stored (must be freed by caller)
 * \return SMTP status code (code >= 400 means error)
*/
int socket_get_smtp_code (SOCKET sock, char** errmsg);

/*! \brief send SMTP command and return status code
 * \param  sock            open network socket
 * \param  debuglog        file handle to write debugging information to (NULL for no debugging)
 * \param  template        printf style formatting template
 * \return SMTP status code (code >= 400 means error)
*/
int socket_smtp_command (SOCKET sock, FILE* debuglog, const char* template, ...);

#ifdef __cplusplus
}
#endif

#endif //__INCLUDED_SMTPSOCKET_H
