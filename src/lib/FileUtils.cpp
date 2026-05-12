#include "FileUtils.h"
#include <Utils.h>
#include <dirent.h>

std::size_t FileUtils::fileSize(const std::string &filename)
{
    struct stat st;
    if (stat(filename.c_str(), &st) < 0) return 0;
    return st.st_size;
}

bool FileUtils::exists(const std::string &filename)
{
    struct stat st;
    if (stat(filename.c_str(), &st) < 0 &&
        filename != "/") // handle /
        return false;
    return true;
}

bool FileUtils::isDir(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) < 0) return false;
    if (S_ISDIR(st.st_mode)) return true;
    return false;
}

bool FileUtils::mkdir(const std::string &path)
{
    static mode_t default_mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    return (::mkdir(path.c_str(), default_mode) == 0);
}

bool FileUtils::mkpath(const std::string &path)
{
    vector<string> tok;
    Utils::split(path, tok, "/");
    string createdPath;

    for (const string &s: tok)
    {
        createdPath += "/";
        createdPath += s;

        if (!FileUtils::exists(createdPath))
        {
            if (!FileUtils::mkdir(createdPath))
                return false;
        }
        else if (!FileUtils::isDir(createdPath))
            return false;
    }

    return true;
}

bool FileUtils::unlink(const std::string &filename)
{
    return ::unlink(filename.c_str()) == 0;
}

bool FileUtils::rmdir(const std::string &path)
{
    return ::rmdir(path.c_str()) == 0;
}

bool FileUtils::rename(const std::string &src, const std::string &dst)
{
    if (::rename(src.c_str(), dst.c_str()))
    {
        if (errno == EXDEV)
            cErrorDom("file") << "Cannot move file to a different mount point: " << src << " --> " << dst;
        return false;
    }
    return true;
}

std::string FileUtils::filename(const std::string &path)
{
    vector<string> tok;
    Utils::split(path, tok, "/");

    if (tok.size() == 0)
        return string();

    return tok.at(tok.size() - 1);
}

bool FileUtils::isReadable(const std::string &path)
{
    return ::access(path.c_str(), R_OK) == 0;
}

bool FileUtils::isWritable(const std::string &path)
{
    return ::access(path.c_str(), W_OK) == 0;
}

bool FileUtils::isExecutable(const std::string &path)
{
    return ::access(path.c_str(), X_OK) == 0;
}

bool FileUtils::copyFile(const std::string &source, const std::string &dest)
{
    FILE *f1, *f2;
    char buf[16384];
    char realpath1[PATH_MAX];
    char realpath2[PATH_MAX];
    size_t num;
    size_t res;

    if (!realpath(source.c_str(), realpath1)) return false;
    if (realpath(dest.c_str(), realpath2) && !strcmp(realpath1, realpath2)) return false;

    f1 = fopen(source.c_str(), "rb");
    if (!f1) return false;
    f2 = fopen(dest.c_str(), "wb");
    if (!f2)
    {
        fclose(f1);
        return false;
    }

    while ((num = fread(buf, 1, sizeof(buf), f1)) > 0)
    {
        res = fwrite(buf, 1, num, f2);
        if (res <= 0) cCriticalDom("file") <<  "Failed to fwrite !";
    }

    fclose(f1);
    fclose(f2);

    return true;
}

std::vector<std::string> FileUtils::listDir(const std::string &path)
{
    std::vector<std::string> ret;
    if (!isDir(path))
        return ret;

    struct dirent *entry;
    DIR *dir = ::opendir(path.c_str());
    if (!dir)
        return ret;

    while ((entry = ::readdir(dir)) != NULL)
    {
        ret.push_back(entry->d_name);
    }
    closedir(dir);

    return ret;
}

bool FileUtils::resolveSafePath(const std::string &root,
                                const std::string &userPath,
                                std::string &outPath)
{
    outPath.clear();

    if (root.empty())
        return false;

    // Reject NUL bytes outright (truncation attacks).
    if (userPath.find('\0') != std::string::npos)
        return false;

    // Reject absolute paths from the user — they would silently escape root
    // when concatenated.
    if (!userPath.empty() && userPath[0] == '/')
        return false;

    // Reject any ".." path segment as defence in depth (before realpath).
    // Split on '/' and check each component.
    {
        std::string::size_type pos = 0;
        while (pos <= userPath.size())
        {
            std::string::size_type next = userPath.find('/', pos);
            std::string seg = userPath.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            if (seg == "..")
                return false;
            if (next == std::string::npos)
                break;
            pos = next + 1;
        }
    }

    // Canonicalize root. Root must exist.
    char realRoot[PATH_MAX];
    if (!realpath(root.c_str(), realRoot))
        return false;

    // Build candidate path.
    std::string candidate = std::string(realRoot);
    if (candidate.empty() || candidate[candidate.size() - 1] != '/')
        candidate += '/';
    candidate += userPath;

    // Canonicalize candidate. Fails if any component is missing → treat as 404.
    char realCandidate[PATH_MAX];
    if (!realpath(candidate.c_str(), realCandidate))
        return false;

    // Ensure the resolved candidate is inside the resolved root.
    // Either it equals the root exactly, or it starts with root + '/'.
    std::string resolved(realCandidate);
    std::string rootStr(realRoot);
    if (resolved != rootStr &&
        (resolved.size() <= rootStr.size() ||
         resolved.compare(0, rootStr.size(), rootStr) != 0 ||
         resolved[rootStr.size()] != '/'))
        return false;

    outPath = resolved;
    return true;
}
