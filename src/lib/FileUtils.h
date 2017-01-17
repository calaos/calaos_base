#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <string>
#include <vector>

class FileUtils
{
public:

    static std::size_t fileSize(const std::string &filename);
    static bool exists(const std::string &filename);
    static bool isDir(const std::string &path);
    static bool mkdir(const std::string &path);
    static bool mkpath(const std::string &path); //full path with subdirs
    static bool rmdir(const std::string &path);
    static bool unlink(const std::string &filename);
    static bool isReadable(const std::string &path);
    static bool isWritable(const std::string &path);
    static bool isExecutable(const std::string &path);
    static bool rename(const std::string &src, const std::string &dst);
    static std::string filename(const std::string &path);
    static bool copyFile(const std::string &src, const std::string &dst);
    static std::vector<std::string> listDir(const std::string &path);
};

#endif // FILEUTILS_H
