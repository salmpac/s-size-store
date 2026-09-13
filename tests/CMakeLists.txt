add_executable(ssize_tests
    test_links.cpp
    test_handlers.cpp
    test_catalog.cpp
    test_events.cpp
    test_migrations.cpp
)

target_link_libraries(ssize_tests PRIVATE ssize_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(ssize_tests)
