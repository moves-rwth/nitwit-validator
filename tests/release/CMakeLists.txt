# The release wrapper nitwit.sh and the smoke test (docs/release.md) with this build's nitwit. CTest label `release`.
add_test(NAME release_archive
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/check_release.sh $<TARGET_FILE:nitwit> ${PROJECT_VERSION}
            ${CMAKE_CURRENT_BINARY_DIR}/archive $<$<BOOL:${NITWIT_STATIC_RUNTIME}>:--glibc-only>)
set_tests_properties(release_archive PROPERTIES
    LABELS release
    ENVIRONMENT "ASAN_OPTIONS=hard_rss_limit_mb=6000")
