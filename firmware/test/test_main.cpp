// Single translation unit that provides the doctest runner main().
// All other test_*.cpp files just #include the doctest header.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
