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
#ifndef S_ACTIONCAMERADOWNLOAD_H
#define S_ACTIONCAMERADOWNLOAD_H

#include "Calaos.h"
#include "ListeRoom.h"
#include "IPCam.h"
#include "UrlDownloader.h"

namespace Calaos
{

/*
 * Camera snapshot downloads, shared by ActionMail and ActionPush.
 *
 * A download runs asynchronously: UrlDownloader spawns curl and emits
 * m_signalComplete from the event loop, then deletes itself through an Idler.
 * The action that started it can be destroyed at any point in between (config
 * reload, IO deletion), so a completion slot capturing the action's `this`
 * would dereference freed memory.
 *
 * Each start() therefore builds its own context, owned by this object, which
 * the actions keep as a member: destroying the action destroys every context,
 * each context disconnects its slot, and no callback can run on a dead action.
 * Contexts are independent, so a rule re-triggering while a first snapshot is
 * still downloading no longer cancels it - both notifications are sent, each
 * with its own picture.
 *
 * A context that is destroyed before its transfer completed also unlinks the
 * file the transfer was writing to: nobody will ever consume it, and on an
 * embedded tmpfs those half written snapshots pile up.
 *
 * T2.10: a context destroyed while its transfer is still pending now calls
 * UrlDownloader::cancel(), which really aborts the transfer (curl is
 * terminated, the pipe closed, the signals cleared) and lets the autodelete
 * object free itself safely. An autodelete downloader must still never be
 * deleted from the outside, cancel() is the only external control.
 */
class ActionCameraDownload
{
public:
    ActionCameraDownload() = default;
    ~ActionCameraDownload() { cancelAll(); }

    ActionCameraDownload(const ActionCameraDownload &) = delete;
    ActionCameraDownload &operator =(const ActionCameraDownload &) = delete;

    //Returns the camera IO named ioId, or nullptr when ioId is empty or is not a camera
    static IPCam *findCamera(const string &ioId)
    {
        if (ioId.empty())
            return nullptr;
        return dynamic_cast<IPCam *>(ListeRoom::Instance().get_io(ioId));
    }

    /* Downloads the current picture of camera into destFile.
     * cb(success, destFile) is called when the transfer completes, success
     * being true when the http status is in the 2xx band. The file is handed
     * over to the callback, which owns it from then on.
     * Returns false when the transfer could not even be started, cb is then
     * never called and destFile has been unlinked.
     */
    bool start(IPCam *camera, const string &destFile, std::function<void(bool, const string &)> cb)
    {
        if (!camera)
            return false;

        if (destFile.empty())
        {
            cWarningDom("rule.action") << "No destination file for the snapshot of "
                                       << camera->get_param("name");
            return false;
        }

        cInfoDom("rule.action") << "Need to download camera ("
                                << camera->get_param("name")
                                << ") attachment";
        cDebugDom("rule.action") << "DL URL: " << camera->getPictureUrl()
                                 << " to " << destFile;

        auto dl = std::make_shared<Download>();
        dl->destFile = destFile;
        downloads.push_back(dl);

        std::weak_ptr<Download> weakDl = dl;

        UrlDownloader *downloader = new UrlDownloader(camera->getPictureUrl(), true);
        dl->downloader = downloader;
        dl->conn = downloader->m_signalComplete.connect([this, weakDl, cb](int status)
        {
            auto ctx = weakDl.lock();
            if (!ctx)
                return; //context dropped, the connection is severed anyway

            //Copies: forgetting the context below releases the slot this
            //lambda lives in
            auto callback = cb;
            string file = ctx->destFile;

            //The transfer is done: the context must not cancel() a downloader
            //that is already freeing itself
            ctx->downloader = nullptr;

            //The file now belongs to the callback, the context must not
            //unlink it on its way out
            ctx->destFile.clear();
            forget(ctx);

            //http success band is [200, 300)
            callback(status >= 200 && status < 300, file);
        });

        if (!downloader->httpGet(destFile))
        {
            cWarningDom("rule.action") << "Failed to start camera download for "
                                       << camera->get_param("name");
            //~Download cancels the transfer and the autodelete object frees
            //itself
            forget(dl);
            return false;
        }

        return true;
    }

    //Severs every pending completion callback and drops the files they were
    //downloading to
    void cancelAll() { downloads.clear(); }

private:
    struct Download
    {
        sigc::connection conn;
        string destFile;
        UrlDownloader *downloader = nullptr; //pending transfer, nulled on completion

        ~Download()
        {
            //Still pending: the action is gone or the transfer never started.
            //Sever the callback and drop the file nobody will read.
            conn.disconnect();

            //T2.10: really abort the transfer, the autodelete object then
            //frees itself safely
            if (downloader)
                downloader->cancel();

            if (!destFile.empty())
                FileUtils::unlink(destFile);
        }
    };

    void forget(const std::shared_ptr<Download> &dl)
    {
        downloads.erase(std::remove(downloads.begin(), downloads.end(), dl), downloads.end());
    }

    std::vector<std::shared_ptr<Download>> downloads;
};

}
#endif
