file_output_test_exe = executable(
    'test_file_output',
    ['test_zstd_output_stream.cpp'],
    include_directories: include_directories('../../src', '..'),
    dependencies: [catch2_with_main_dep, libtcspc_dep, libzstd_dep],
)

test('file_output', file_output_test_exe, timeout: 60)
