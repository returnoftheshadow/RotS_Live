#include "test_random_utils.h"
#include <deque>

namespace {

std::deque<double> test_random_values;

double clamp_test_random_value(double value)
{
    if (value < 0.0) {
        return 0.0;
    }

    if (value >= 1.0) {
        return 0.999999;
    }

    return value;
}

} // namespace

void clear_test_random_values()
{
    test_random_values.clear();
}

void push_test_random_value(double value)
{
    test_random_values.push_back(value);
}

extern "C" double __real__Z6numberv();
extern "C" int __real__Z6numberii(int from, int to);

extern "C" double __wrap__Z6numberv()
{
    if (!test_random_values.empty()) {
        double value = test_random_values.front();
        test_random_values.pop_front();
        return clamp_test_random_value(value);
    }

    return __real__Z6numberv();
}

extern "C" int __wrap__Z6numberii(int from, int to)
{
    if (from == to) {
        return from;
    }

    if (from > to) {
        int tmp = from;
        from = to;
        to = tmp;
    }

    int upper_end = to - from + 1;
    if (upper_end == 0) {
        to = from;
        upper_end = 1;
    }

    if (!test_random_values.empty()) {
        double value = clamp_test_random_value(test_random_values.front());
        test_random_values.pop_front();
        return from + static_cast<int>(value * upper_end);
    }

    return __real__Z6numberii(from, to);
}
