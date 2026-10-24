# Contains tests for the simulated time tagger
subdir('fake_timetagger')
# Contains tests for the acquisition lifecycle components
subdir('acquisition_lifecycle')
# Contains tests for the OpenScan module
subdir('openscan_module')
# Contains tests for file output streams
subdir('file_output')

# Smoke tests for tools/dump_graph.cpp, one per graph shape
dump_graph_variants = {
    'default': [],
    'cumulative_intensity': ['--cumulative-intensity'],
    'all_outputs': ['--histogram-file', '--tag-stream-file'],
    'cumulative_intensity_all_outputs': [
        '--cumulative-intensity',
        '--histogram-file',
        '--tag-stream-file',
    ],
    'four_detectors': ['--detectors', '4'],
    'four_detectors_cumulative_intensity_all_outputs': [
        '--detectors', '4',
        '--cumulative-intensity',
        '--histogram-file',
        '--tag-stream-file',
    ],
}
foreach name, args : dump_graph_variants
    test('dump_graph_' + name, dump_graph_exe, args: args)
endforeach
