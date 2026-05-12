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

    // Safely resolve a user-supplied sub-path against a trusted root directory.
    // Rejects absolute paths, NUL bytes, and any ".." segment before touching
    // the filesystem, then canonicalizes both root and candidate with realpath()
    // and ensures the resolved candidate stays inside the resolved root.
    // Returns true on success and writes the canonical absolute path to outPath.
    // Returns false on any traversal attempt, missing file, or symlink that
    // escapes the root. Caller should treat false as "not found" (HTTP 404)
    // to avoid leaking filesystem layout.
    static bool resolveSafePath(const std::string &root,
                                const std::string &userPath,
                                std::string &outPath);
};

#endif // FILEUTILS_H
