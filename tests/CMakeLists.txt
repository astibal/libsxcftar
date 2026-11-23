find_package(GTest REQUIRED)
add_executable(sxcftar_tests
    crypto_tests.cpp
    unit/service/cfg_tar_tests.cpp
    unit/service/cfg_archive_tests.cpp
    unit/service/cfg_archive_session_tests.cpp
    unit/service/cfg_pack_tests.cpp
    unit/service/cfg_runtime_tree_tests.cpp)
target_link_libraries(sxcftar_tests PRIVATE sxcftar GTest::gtest_main LibArchive::LibArchive)
add_test(NAME sxcftar.unit COMMAND sxcftar_tests)
add_executable(sxcftar_io_fault_tests unit/service/cfg_io_fault_tests.cpp)
target_link_libraries(sxcftar_io_fault_tests PRIVATE sxcftar GTest::gtest_main LibArchive::LibArchive)
        target_link_options(sxcftar_io_fault_tests PRIVATE
                -Wl,--wrap=open -Wl,--wrap=openat
                -Wl,--wrap=fstat -Wl,--wrap=fstatat -Wl,--wrap=lstat
                -Wl,--wrap=read -Wl,--wrap=write -Wl,--wrap=fsync
                -Wl,--wrap=dup -Wl,--wrap=fdopendir -Wl,--wrap=readdir
                -Wl,--wrap=mkdir -Wl,--wrap=mkdirat
                -Wl,--wrap=archive_read_new
                -Wl,--wrap=archive_read_support_format_tar
                -Wl,--wrap=archive_read_open_memory
                -Wl,--wrap=archive_read_next_header
                -Wl,--wrap=archive_errno -Wl,--wrap=archive_entry_pathname
                -Wl,--wrap=archive_read_data
                -Wl,--wrap=archive_write_new
                -Wl,--wrap=archive_write_set_format_pax_restricted
                -Wl,--wrap=archive_write_open -Wl,--wrap=archive_entry_new
                -Wl,--wrap=archive_write_header -Wl,--wrap=archive_write_data
                -Wl,--wrap=archive_write_close)
add_test(NAME sxcftar.io_faults COMMAND sxcftar_io_fault_tests)
