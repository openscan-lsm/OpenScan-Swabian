# Unit tests for the acquisition lifecycle components, built against the
# fake SDK regardless of the `simulate` option (like tests/fake_timetagger),
# since they only need IteratorBase and TimeTaggerBase, not hardware.
acquisition_lifecycle_test_srcs = [
    '../../src/ProcessingThread.cpp',
    '../../src/TagStreamMeasurement.cpp',
    '../../src/TagStreamProcessor.cpp',
    'test_processing_thread.cpp',
    'test_tag_stream_measurement.cpp',
    'test_tag_stream_processor.cpp',
]

acquisition_lifecycle_test_exe = executable(
    'test_acquisition_lifecycle',
    acquisition_lifecycle_test_srcs,
    include_directories: include_directories(
        '../../src/fake_timetagger/include',
        '../../src',
    ),
    dependencies: [
        catch2_with_main_dep,
        libtcspc_dep,
    ],
)

test(
    'acquisition_lifecycle',
    acquisition_lifecycle_test_exe,
    timeout: 60,
)
