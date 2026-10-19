# Contains tests for the simulated time tagger
subdir('fake_timetagger')
# Contains tests for the acquisition lifecycle components
subdir('acquisition_lifecycle')
# Contains tests for the OpenScan module
subdir('openscan_module')

# Smoke tests for tools/dump_graph.cpp, one per graph shape
dump_graph_variants = {
    'default': [],
    'cumulative': ['--cumulative'],
    'all_outputs': ['--histogram-dump', '--raw-data'],
    'cumulative_all_outputs': ['--cumulative', '--histogram-dump', '--raw-data'],
}
foreach name, args : dump_graph_variants
    test('dump_graph_' + name, dump_graph_exe, args: args)
endforeach
