add_executable(referee_scheduler_tests
    test_smoke.cpp
)

add_test(NAME referee_scheduler_smoke COMMAND referee_scheduler_tests)
