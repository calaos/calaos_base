/*! \file      quickmailprog.c
 *  \brief     source file of quickmail(light) application
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
/*! \page     quickmail quickmail - send e-mail from the command line
    \details  Send e-mail from the command line.
              Features:
               - multiple To/Cc/Bcc recipients
               - multiple attachments without size limitation
               - specifying the MIME-type to use for the message body
    \section SYNOPSIS synopsis
              quickmail -h server [-p port] [-u username] [-w password] -f email [-t email] [-c email] [-b email] [-s subject] [-m mimetype] [-d body] [-a file] [-v] [-?]
    \section OPTIONS options
              \verbatim
               -h server      hostname or IP address of SMTP server
               -p port        TCP port to use for SMTP connection (default is 25)
               -u username    username to use for SMTP authentication
               -w password    password to use for SMTP authentication
               -f email       From e-mail address
               -t email       To e-mail address (multiple -t can be specified)
               -c email       Cc e-mail address (multiple -c can be specified)
               -b email       Bcc e-mail address (multiple -b can be specified)
               -s subject     Subject
               -m mimetype    MIME used for the body (must be specified before -d)
               -d body        Body, if not specified will be read from standard input
               -a file        file to attach (multiple -a can be specified)
               -v             verbose mode
               -?             show help
              \endverbatim
 */

#include "quickmail.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#ifndef NOCURL
#include <curl/curl.h>
#endif

void show_help()
{
  printf(
    "Usage:  quickmail {-h server | -o filename} [-p port] [-u username] [-w password] -f email [-t email] [-c email] [-b email] [-s subject] [-m mimetype] [-d body] [-a file] [-v]\n" \
    "Parameters:\n" \
    "  -h server   \thostname or IP address of SMTP server\n" \
    "  -o filename \tname of file to dump the mail content to (- for stdout)\n" \
    "  -p port     \tTCP port to use for SMTP connection (default is 25)\n" \
    "  -u username \tusername to use for SMTP authentication\n" \
    "  -w password \tpassword to use for SMTP authentication\n" \
    "  -f email    \tFrom e-mail address\n" \
    "  -t email    \tTo e-mail address (multiple -t can be specified)\n" \
    "  -c email    \tCc e-mail address (multiple -c can be specified)\n" \
    "  -b email    \tBcc e-mail address (multiple -b can be specified)\n" \
    "  -s subject  \tSubject\n" \
    "  -m mimetype \tMIME used for the body (must be specified before -d)\n" \
    "  -d body     \tbody, if not specified will be read from standard input\n" \
    "  -a file     \tfile to attach (multiple -a can be specified)\n" \
    "  -v          \tverbose mode\n" \
    "  -?          \tshow help\n" \
    "\n"
  );
}

size_t email_info_attachment_read_stdin (void* handle, void* buf, size_t len)
{
  return fread(buf, 1, len, stdin);
}

int main (int argc, char *argv[])
{
  //default values
  FILE* output_file = NULL;
  const char* smtp_server = NULL;
  int smtp_port = 25;
  const char* smtp_username = NULL;
  const char* smtp_password = NULL;
  const char* mime_type = NULL;
  char* body = NULL;

  //show version
#ifdef NOCURL
  printf("quickmail %s\n", quickmail_get_version());
#else
  {
    curl_version_info_data* curlversion = curl_version_info(CURLVERSION_NOW);
    printf("quickmail %s (with libcurl %s)\n", quickmail_get_version(), (curlversion ? curlversion->version : curl_version()));
  }
#endif
  //initialize mail object
  quickmail_initialize();
  quickmail mailobj = quickmail_create(NULL, NULL);

  //process command line parameters
  {
    int i = 0;
    char* param;
    int paramerror = 0;
    unsigned recipient_count = 0;
    while (!paramerror && ++i < argc) {
      if (!argv[i][0] || (argv[i][0] != '/' && argv[i][0] != '-')) {
        paramerror++;
        break;
      } else {
        param = NULL;
        switch (tolower(argv[i][1])) {
          case '?' :
            show_help();
            return 0;
          case 'o' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param || !*param || strcmp(param, "-") == 0)
              output_file = stdout;
            else
              if ((output_file = fopen(param, "wb")) == NULL)
                fprintf(stderr, "Error writing to file: %s\n", param);
            break;
          case 'h' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param)
              paramerror++;
            else
              smtp_server = param;
            break;
          case 'p' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param)
              paramerror++;
            else
              smtp_port = atoi(param);
            break;
          case 'u' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param)
              paramerror++;
            else
              smtp_username = param;
            break;
          case 'w' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param)
              paramerror++;
            else
              smtp_password = param;
            break;
          case 'f' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param)
              paramerror++;
            else
              quickmail_set_from(mailobj, param);
            break;
          case 't' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param) {
              paramerror++;
            } else {
              quickmail_add_to(mailobj, param);
              recipient_count++;
            }
            break;
          case 'c' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param) {
              paramerror++;
            } else {
              quickmail_add_cc(mailobj, param);
              recipient_count++;
            }
            break;
          case 'b' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param) {
              paramerror++;
            } else {
              quickmail_add_bcc(mailobj, param);
              recipient_count++;
            }
            break;
          case 's' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param)
              paramerror++;
            else
              quickmail_set_subject(mailobj, param);
            break;
          case 'm' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param)
              paramerror++;
            else
              mime_type = param;
            break;
          case 'd' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param)
              paramerror++;
            else if (strcmp(param, "-") != 0)
              body = param;
            break;
          case 'a' :
            if (argv[i][2])
              param = argv[i] + 2;
            else if (i + 1 < argc && argv[i + 1])
              param = argv[++i];
            if (!param)
              paramerror++;
            else
              quickmail_add_attachment_file(mailobj, param, NULL);
            break;
          case 'v' :
            quickmail_set_debug_log(mailobj, stdout);
            break;
          default :
            paramerror++;
            break;
        }
      }
    }
    if (paramerror || (!smtp_server && !output_file) || !quickmail_get_from(mailobj)) {
      fprintf(stderr, "Invalid command line parameters\n");
      show_help();
      return 1;
    }
    if (recipient_count == 0) {
      fprintf(stderr, "At least one recipient (To/Cc/Bcc) must be specified\n");
      return 1;
    }
  }
  //read body from standard input if not given
  if (body) {
    quickmail_add_body_memory(mailobj, mime_type, body, strlen(body), 0);
  } else {
    quickmail_add_body_custom(mailobj, mime_type, NULL, NULL, email_info_attachment_read_stdin, NULL, NULL);
  }
  mime_type = NULL;
  //send e-mail
  int status = 0;
  if (smtp_server) {
    const char* errmsg;
    if ((errmsg = quickmail_send(mailobj, smtp_server, smtp_port, smtp_username, smtp_password)) != NULL) {
      status = 1;
      fprintf(stderr, "Error sending e-mail: %s\n", errmsg);
    }
  }
  //write e-mail body
  if (output_file) {
    quickmail_fsave(mailobj, output_file);
  }
  //clean up
  quickmail_destroy(mailobj);
  return status;
}
