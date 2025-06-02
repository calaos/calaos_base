#include "ExpressionEvaluator.h"
#include <gtest/gtest.h>

class ExpressionEvaluatorTest: public ::testing::Test
{
protected:
    virtual void SetUp()
    {}

    virtual void TearDown()
    {}

};

TEST_F(ExpressionEvaluatorTest, isExpressionValid)
{
    EXPECT_FALSE(ExpressionEvaluator::isExpressionValid("2 +"));
    EXPECT_FALSE(ExpressionEvaluator::isExpressionValid("2 + 3 *"));
    EXPECT_TRUE(ExpressionEvaluator::isExpressionValid("2 + 3 * 4"));
    EXPECT_TRUE(ExpressionEvaluator::isExpressionValid("x+3/4"));
    EXPECT_TRUE(ExpressionEvaluator::isExpressionValid("x + 3 / 4"));
    EXPECT_TRUE(ExpressionEvaluator::isExpressionValid("x * 255 / 100"));
    EXPECT_FALSE(ExpressionEvaluator::isExpressionValid("x * 255 / 100 +"));
    EXPECT_TRUE(ExpressionEvaluator::isExpressionValid("value * 255 / 100"));
    EXPECT_FALSE(ExpressionEvaluator::isExpressionValid("value * 255 / 100 +"));
}

TEST_F(ExpressionEvaluatorTest, calculateExpression)
{
    bool failed = true;
    EXPECT_EQ(0.0, ExpressionEvaluator::calculateExpression("2 +", 10, failed));
    EXPECT_TRUE(failed);
    EXPECT_EQ(0.0, ExpressionEvaluator::calculateExpression("2 + 3 *", 10, failed));
    EXPECT_TRUE(failed);
    EXPECT_EQ(14.0, ExpressionEvaluator::calculateExpression("2 + 3 * 4", 10, failed));
    EXPECT_FALSE(failed);
    EXPECT_EQ(10.75, ExpressionEvaluator::calculateExpression("x+3/4", 10, failed));
    EXPECT_FALSE(failed);
    EXPECT_EQ(11.75, ExpressionEvaluator::calculateExpression("x + 3 / 4", 11, failed));
    EXPECT_FALSE(failed);
    EXPECT_EQ(76.5, ExpressionEvaluator::calculateExpression("x * 255 / 100", 30, failed));
    EXPECT_FALSE(failed);
    EXPECT_EQ(76.5, ExpressionEvaluator::calculateExpression("value * 255 / 100", 30, failed));
    EXPECT_FALSE(failed);
}

TEST_F(ExpressionEvaluatorTest, evaluateExpressionBool)
{
    bool failed = false;
    EXPECT_TRUE(ExpressionEvaluator::evaluateExpressionBool("x > 0", "10", failed));
    EXPECT_FALSE(failed);
    EXPECT_FALSE(ExpressionEvaluator::evaluateExpressionBool("x < 0", "10", failed));
    EXPECT_FALSE(failed);
    EXPECT_TRUE(ExpressionEvaluator::evaluateExpressionBool("value == 'connected'", "connected", failed));
    EXPECT_FALSE(failed);
    EXPECT_FALSE(ExpressionEvaluator::evaluateExpressionBool("value == 'disconnected'", "connected", failed));
    EXPECT_FALSE(failed);
    EXPECT_TRUE(ExpressionEvaluator::evaluateExpressionBool("x > 5 and x < 15", "10", failed));
    EXPECT_FALSE(failed);
    EXPECT_FALSE(ExpressionEvaluator::evaluateExpressionBool("value > 15 or value < 5", "10", failed));
    EXPECT_FALSE(failed);
    EXPECT_FALSE(ExpressionEvaluator::evaluateExpressionBool("value == 'on' or value > 50", "on", failed));
    EXPECT_TRUE(failed);
    EXPECT_FALSE(ExpressionEvaluator::evaluateExpressionBool("value == 'on' or value > 50", "65", failed));
    EXPECT_TRUE(failed);

    EXPECT_FALSE(ExpressionEvaluator::evaluateExpressionBool("y > 0 and values == 'on'", "10", failed));
    EXPECT_TRUE(failed);
    EXPECT_FALSE(ExpressionEvaluator::evaluateExpressionBool("value * 255", "10", failed));
    EXPECT_TRUE(failed);
    EXPECT_FALSE(ExpressionEvaluator::evaluateExpressionBool("value * 255", "10", failed));
    EXPECT_TRUE(failed);
    EXPECT_FALSE(ExpressionEvaluator::evaluateExpressionBool("value * 255", "connected", failed));
    EXPECT_TRUE(failed);
}
