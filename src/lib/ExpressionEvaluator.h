#pragma once

#include <string>
#include <vector>

class ExpressionEvaluator
{
public:

    static bool isExpressionValid(const std::string &expression);
    static double calculateExpression(const std::string &expression, double rawValue, bool &failed);
    static bool evaluateExpressionBool(const std::string &expression, const std::string &rawValue, bool &failed);
};
