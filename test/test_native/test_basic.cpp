#include <unity.h>

static int add(int a, int b) { return a + b; }

void test_add(void) {
  TEST_ASSERT_EQUAL(4, add(2, 2));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_add);
  return UNITY_END();
}
