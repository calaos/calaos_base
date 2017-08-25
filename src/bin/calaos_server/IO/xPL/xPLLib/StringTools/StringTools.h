#ifndef StringTools_H
#define StringTools_H

#include<vector>
#include<string>

class StringTools
{
    public:
        static std::string& ltrim(std::string& s);
        static std::string& rtrim(std::string& s);
        static std::string& trim(std::string& s);
        static std::string ReplaceAll(std::string& strExpression, const std::string& strFind, const std::string& strReplace);
        static bool Split(std::string const& source, char const delimiter, std::string* key, std::string* value);
        static std::vector<std::string> Split(const std::string &s, char delim);
        static void ParasitCar(std::string& str);
        static bool IsEqualCaseInsensitive(std::string const& a, std::string const& b);
        static std::string& ToLower(std::string& data);
        static std::string& ToUpper(std::string& data);
        static bool IsNumber(const std::string& s);
        static std::string to_string(int n);
        static int stoi(const std::string& s);
};
#endif
