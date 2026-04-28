add_test_executable(
    TEST_NAME sse
    SOURCES
        test_sse.cpp
    INCLUDES
        ../include
    LIBRARIES
        beauty::beauty
)
