rate_processors_test_exe = executable(
    'test_rate_processors',
    'test_rate_processors.cpp',
    include_directories: include_directories('../../src'),
    dependencies: [
        catch2_with_main_dep,
        libtcspc_dep,
    ],
)

test('rate_processors', rate_processors_test_exe)
