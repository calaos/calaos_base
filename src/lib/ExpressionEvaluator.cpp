#include "ExpressionEvaluator.h"
#include <exprtk.hpp>
#include "Utils.h"

bool ExpressionEvaluator::isExpressionValid(const std::string &expression)
{
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    double x = 0.0;

    symbol_table_t symbol_table;
    symbol_table.add_variable("x", x);

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

bool ExpressionEvaluator::evaluateExpressionBool(const std::string &expression, const std::string &rawValue, bool &failed)
{
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    bool result = false;
    failed = true;

    symbol_table_t symbol_table;

    // Add string variable for comparisons like "value == 'connected'"
    std::string value_str = rawValue;
    symbol_table.add_stringvar("value", value_str);

    // Try to convert rawValue to double for numeric comparisons
    double value_num = 0.0;
    try
    {
        value_num = std::stod(rawValue);
        symbol_table.add_variable("value", value_num);
    }
    catch (const std::exception&)
    {
        // If conversion fails, just use string comparison
        double zero_value = 0.0;
        symbol_table.add_variable("value", zero_value);
    }

    expression_t expr;
    expr.register_symbol_table(symbol_table);

    parser_t parser;

    if (!parser.compile(expression, expr))
    {
        cWarningDom("expr") << "Invalid boolean expression: " << parser.error();
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
        cWarningDom("expr") << "Error evaluating boolean expression: " << e.what();
    }

    return result;
}
