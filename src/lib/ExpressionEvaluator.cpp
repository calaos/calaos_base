#include "ExpressionEvaluator.h"
#include <exprtk.hpp>
#include "Utils.h"
#include <charconv>
#include <string>

bool ExpressionEvaluator::isExpressionValid(const std::string &expression)
{
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    double x = 0.0;

    symbol_table_t symbol_table;
    symbol_table.add_variable("x", x);
    symbol_table.add_variable("value", x);

    expression_t expr;
    expr.register_symbol_table(symbol_table);

    parser_t parser;

    if (!parser.compile(expression, expr))
    {
        cWarningDom("expr") << "Invalid expression: " << parser.error();
        return false;
    }

    return true;
}

double ExpressionEvaluator::calculateExpression(const std::string &expression, double rawValue, bool &failed)
{
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    double result = 0.0;
    failed = true;

    symbol_table_t symbol_table;
    symbol_table.add_variable("x", rawValue);
    symbol_table.add_variable("value", rawValue);

    expression_t expr;
    expr.register_symbol_table(symbol_table);

    parser_t parser;

    if (!parser.compile(expression, expr))
    {
        cWarningDom("expr") << "Invalid expression: " << parser.error();
        return result;
    }

    try
    {
        result = expr.value();
        failed = false;
    }
    catch (const std::exception &e)
    {
        cWarningDom("expr") << "Error evaluating expression: " << e.what();
    }

    return result;
}

static bool parse_double(const std::string& rawValue, double& value_num)
{
    const char* begin = rawValue.data();
    const char* end   = rawValue.data() + rawValue.size();

    auto [ptr, ec] = std::from_chars(begin, end, value_num);

    if (ec != std::errc() || ptr != end)
        return false;

    return true;
}

bool ExpressionEvaluator::evaluateExpressionBool(const std::string &expression, const std::string &rawValue, bool &failed)
{
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    bool result  = false;
    failed = true;

    // 1) quick check: must contain a boolean operator
    static const std::vector<std::string> bool_ops =
        { "==", "!=", "<=", ">=", " and ", " or ", "<", ">" };
    bool has_bool_op = false;
    for (auto &op : bool_ops)
    {
        if (expression.find(op) != std::string::npos)
        {
            has_bool_op = true;
            break;
        }
    }

    if (!has_bool_op)
    {
        cWarningDom("expr") << "No boolean operator in expression: " << expression;
        return false;
    }

    // 2) detect rawValue type
    double value_num = 0.0;
    bool is_numeric = parse_double(rawValue, value_num);

    symbol_table_t symbol_table;
    if (is_numeric)
    {
        // only numeric
        if (!symbol_table.add_variable("x",     value_num))
        {
            cWarningDom("expr") << "Failed to add numeric variable 'x'";
            return false;
        }

        if (!symbol_table.add_variable("value", value_num))
        {
            cWarningDom("expr") << "Failed to add numeric variable 'value'";
            return false;
        }
    }
    else
    {
        // only string
        std::string value_str = Utils::trim(rawValue);
        if (!symbol_table.add_stringvar("x",     value_str))
        {
            cWarningDom("expr") << "Failed to add string variable 'x'";
            return false;
        }
        if (!symbol_table.add_stringvar("value", value_str))
        {
            cWarningDom("expr") << "Failed to add string variable 'value'";
            return false;
        }
    }

    expression_t expr;
    if (!expr.register_symbol_table(symbol_table))
    {
        cWarningDom("expr") << "Failed to register symbol table";
        return false;
    }

    parser_t parser;

    if (!parser.compile(expression, expr))
    {
        cWarningDom("expr") << "Invalid boolean expression: "
                            << parser.error();
        return result;
    }

    try
    {
        double eval_result = expr.value();
        // Convert numeric result to boolean (0 = false, non-zero = true)
        result = (eval_result != 0.0);
        failed = false;
    }
    catch (const std::exception &e)
    {
        cWarningDom("expr") << "Error evaluating boolean expression: "
                            << e.what();
    }

    return result;
}
