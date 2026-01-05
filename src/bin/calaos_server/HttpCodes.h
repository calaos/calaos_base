/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#ifndef S_HttpCodes_H
#define S_HttpCodes_H

#define HTTP_400 "HTTP/1.0 400 Bad Request"
#define HTTP_401 "HTTP/1.0 401 Unauthorized"
#define HTTP_403 "HTTP/1.0 403 Forbidden"
#define HTTP_404 "HTTP/1.0 404 Not Found"
#define HTTP_429 "HTTP/1.0 429 Too Many Requests"
#define HTTP_301 "HTTP/1.1 301 Moved Permanently"
#define HTTP_500 "HTTP/1.0 500 Internal Server Error"
#define HTTP_200 "HTTP/1.0 200 OK"
#define HTTP_WS_HANDSHAKE "HTTP/1.1 101 Switching Protocols"

#define HTTP_400_BODY "<html><head>" \
    "<title>400 Bad Request</title>" \
    "</head>" \
    "<body>" \
    "<h1>Calaos Server - Bad Request</h1>" \
    "<p>The server received a request it could not understand.</p>" \
    "</body>" \
    "</html>"

#define HTTP_404_BODY "<html><head>" \
    "<title>404 Not Found</title>" \
    "</head>" \
    "<body>" \
    "<h1>Calaos Server - Page not found</h1>" \
    "<p>Document or file requested by the client was not found.</p>" \
    "</body>" \
    "</html>"

#define HTTP_500_BODY "<html><head>" \
    "<title>500 Internal Server Error</title>" \
    "</head>" \
    "<body>" \
    "<h1>Calaos Server - Internal Server Error</h1>" \
    "<p>The server encountered an unexpected condition which prevented it from fulfilling the request.</p>" \
    "</body>" \
    "</html>"

#define HTTP_CAMERA_STREAM "HTTP/1.0 200 OK\r\n" \
    "Server: Calaos/1.0\r\n" \
    "Connection: close\r\n" \
    "Content-Type: multipart/x-mixed-replace;boundary=CalaosBoundary\r\n" \
    "Pragma: no-cache\r\n" \
    "Cache-Control: no-cache\r\n" \
    "Expires: 01 Jan 1970 00:00:00 GMT\r\n" \

#define HTTP_CAMERA_STREAM_BOUNDARY "\r\n" \
    "--CalaosBoundary\r\n" \
    "Content-Type: image/jpeg\r\n" \
    "\r\n"

#endif
