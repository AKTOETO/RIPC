#include <gtest/gtest.h>
#include <ripc/ripc.hpp>

class RipcTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        ripc::shutdown(); // Гарантированная очистка перед каждым тестом
        ripc::initialize();
    }

    void TearDown() override
    {
        ripc::shutdown(); // Корректное завершение после каждого теста
    }
};