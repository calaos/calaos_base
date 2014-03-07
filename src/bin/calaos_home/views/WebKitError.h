/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifndef WEBKITERROR_H
#define WEBKITERROR_H

#define WEBKIT_ERROR_HTML " \
<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\"> \
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"fr\" lang=\"fr\"> \
             <head> \
             <meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\" /> \
                               <title>Erreur de chargement !</title> \
                               <style type=\"text/css\"> \
                                            body \
{ \
                                            margin: 10px 0; \
                                            padding: 0; \
                                            text-align: center; \
                                            background-color: #454545; \
                                            } \
                                            .bloc \
{ \
                                            margin: 50px auto; \
                                            text-align: left; \
                                            padding: 10px; \
                                            border: 10px solid #3ab4d7; \
                                            background-color: #2a2a2a; \
                                            -webkit-border-radius: 10px; \
                                            width: 600px; \
                                            color: #fff; \
                                            } \
                                            .bloc h1 \
{ \
                                            color: #fff; \
                                            margin-left: 10px; \
                                            } \
                                            .bloc hr \
{ \
                                            border: 1px solid #297e97; \
                                            } \
                                            .bloc li \
{ \
                                            padding: 5px; \
                                            } \
                                            </style> \
                                            </head> \
                                            <body> \
                                            <div class=\"bloc\"> \
                                                        <h1>Erreur !</h1> \
                                                        <p> \
                                                        Une erreur est survenue lors de la connexion au site: \
                                                        </p> \
                                                        <hr /> \
                                                        <strong>{FAILING_URL}</strong><br/>{DESC} \
                                                        <hr /> \
                                                        Le navigateur ne peut pas ouvrir la page du site web demandé. \
                                                        <ul> \
                                                        <li>Avez vous correctement tapé l'adresse du site web? (ex: \"<strong>ww</strong>.calaos.fr\" au lieu de \"<strong>www</strong>.calaos.fr)</li> \
                                                        <li>Etes-vous sûr que ce site existe?</li> \
                                                                                             <li>Est-ce que votre connection Internet est opérationnelle? Veuillez vérifier connection Internet.</li> \
                                                                                                                                                          <li>La centrale Calaos est-elle bien paramétrée pour accéder à Internet? Vérifiez les réglages.</li> \
                                                                                                                                                                                                                                   </ul> \
                                                                                                                                                                                                                                   </div> \
                                                                                                                                                                                                                                   </body> \
                                                                                                                                                                                                                                   </html> \
                                                                                                                                                                                                                                   "

                                                                                                                                                                                                                                   #endif
