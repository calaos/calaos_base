/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include <TCPConnection.h>

using namespace Calaos;

void TCPConnection::CameraCommand(Params &request, ProcessDone_cb callback)
{
    Params result = request;

    //camera functions
    if (request["0"] == "camera")
    {
        cDebugDom("network") << "camera";
        if (request["1"] == "?")
        {
            int cpt = 0;
            for (int i = 0;i < ListeRoom::Instance().get_nb_input();i++)
            {
                Input *in = ListeRoom::Instance().get_input(i);
                CamInput *ipcam = dynamic_cast<CamInput *>(in);
                if (ipcam)
                    cpt++;
            }

            result.Add("1", Utils::to_string(cpt));
        }
        else if (request["1"] == "get")
        {
            if (Utils::is_of_type<int>(request["2"]))
            {
                int id, cpt = 0;
                IPCam *camera = NULL;
                Utils::from_string(request["2"], id);

                for (int i = 0;i < ListeRoom::Instance().get_nb_input();i++)
                {
                    Input *in = ListeRoom::Instance().get_input(i);
                    CamInput *ipcam = dynamic_cast<CamInput *>(in);
                    if (ipcam)
                        cpt++;

                    if (cpt == id + 1)
                    {
                        camera = ipcam->get_cam();
                        break;
                    }
                }

                if (camera)
                {
                    result.Add("1", Utils::to_string(id));
                    result.Add("2", string("id:") + camera->get_param("id"));
                    result.Add("3", string("name:") + camera->get_param("name"));
                    cpt = 4;
                    if (camera->get_cam_url() != "")
                    {
                        result.Add(Utils::to_string(cpt), string("url:") + camera->get_cam_url());
                        cpt++;
                    }
                    if (camera->get_mjpeg_stream() != "")
                    {
                        result.Add(Utils::to_string(cpt), string("mjpeg_url:") + camera->get_mjpeg_stream());
                        cpt++;
                    }
                    if (camera->get_mpeg_stream() != "")
                    {
                        result.Add(Utils::to_string(cpt), string("mpeg_url:") + camera->get_mpeg_stream());
                        cpt++;
                    }
                    if (camera->get_picture() != "")
                    {
                        result.Add(Utils::to_string(cpt), string("jpeg_url:") + camera->get_picture());
                        cpt++;
                    }

                    Params caps = camera->getCapabilities();
                    if (caps["ptz"] == "true")
                    {
                        result.Add(Utils::to_string(cpt), "ptz:true");
                        cpt++;
                    }
                    else
                    {
                        result.Add(Utils::to_string(cpt), "ptz:false");
                        cpt++;
                    }

                    result.Add(Utils::to_string(cpt), string("iid:") + camera->get_param("iid"));
                    cpt++;
                    result.Add(Utils::to_string(cpt), string("oid:") + camera->get_param("oid"));
                    cpt++;
                }
            }
        }
        else if (request["1"] == "capabilities")
        {
            IPCam *camera = NULL;
            if (Utils::is_of_type<int>(request["2"]))
            {
                int id, cpt = 0;
                Utils::from_string(request["2"], id);

                for (int i = 0;i < ListeRoom::Instance().get_nb_input();i++)
                {
                    Input *in = ListeRoom::Instance().get_input(i);
                    CamInput *ipcam = dynamic_cast<CamInput *>(in);
                    if (ipcam)
                        cpt++;

                    if (cpt == id + 1)
                    {
                        camera = ipcam->get_cam();
                        break;
                    }
                }
            }

            //give the list of capabilities for camera
            if (camera && request["3"] == "get")
            {
                Params p = camera->getCapabilities();
                int cpt = 3;

                for (int i = 0;i < p.size();i++)
                {
                    std::string key, value;
                    p.get_item(i, key, value);

                    key += ":" + value;
                    result.Add(Utils::to_string(cpt), key);
                    cpt++;
                }
            }

            //set a camera options
            if (camera && request["3"] == "set")
            {
                for (int i = 4;i < request.size();i++)
                {
                    std::string p = request[Utils::to_string(i)];
                    vector<string> splitter;
                    Utils::split(p, splitter, ":", 2);

                    for_each(splitter.begin(), splitter.end(), UrlDecode());

                    if (splitter.size() > 1)
                        camera->activateCapabilities(splitter[0], "set", splitter[1]);
                }

                result.Add("4", "ok");

                for (int i = 5;i < request.size();i++)
                    result.Add(Utils::to_string(i), ""); //clear
            }
        }
        else if (request["1"] == "move")
        {
            if (Utils::is_of_type<int>(request["2"]))
            {
                int id, cpt = 0;
                IPCam *camera = NULL;
                Utils::from_string(request["2"], id);

                for (int i = 0;i < ListeRoom::Instance().get_nb_input();i++)
                {
                    Input *in = ListeRoom::Instance().get_input(i);
                    CamInput *ipcam = dynamic_cast<CamInput *>(in);
                    if (ipcam)
                        cpt++;

                    if (cpt == id + 1)
                    {
                        camera = ipcam->get_cam();
                        break;
                    }
                }

                if (camera)
                {
                    int action = -1;

                    if (request["3"] == "left") action = 1;
                    if (request["3"] == "right") action = 1;
                    if (request["3"] == "up") action = 1;
                    if (request["3"] == "down") action = 1;
                    if (request["3"] == "home") action = 1;
                    if (request["3"] == "zoomin") action = 1;
                    if (request["3"] == "zoomout") action = 1;

                    if (action == 1)
                        camera->activateCapabilities("ptz", "move", request["3"]);

                    //move to a preset position
                    if (Utils::is_of_type<int>(request["3"]))
                        camera->activateCapabilities("position", "recall", request["3"]);

                    result.Add("3", "ok");
                }
            }
        }
        else if (request["1"] == "save")
        {
            if (Utils::is_of_type<int>(request["2"]))
            {
                int id, cpt = 0;
                IPCam *camera = NULL;
                Utils::from_string(request["2"], id);

                for (int i = 0;i < ListeRoom::Instance().get_nb_input();i++)
                {
                    Input *in = ListeRoom::Instance().get_input(i);
                    CamInput *ipcam = dynamic_cast<CamInput *>(in);
                    if (ipcam)
                        cpt++;

                    if (cpt == id + 1)
                    {
                        camera = ipcam->get_cam();
                        break;
                    }
                }

                if (camera)
                {
                    //save position to a preset
                    if (Utils::is_of_type<int>(request["3"]))
                        camera->activateCapabilities("position", "save", request["3"]);

                    result.Add("3", "ok");
                }
            }
        }
    }

    //return the result
    ProcessDone_signal sig;
    sig.connect(callback);
    sig.emit(result);
}
