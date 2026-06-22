test_settings = executable(
  'test_settings',
  ['test_settings.c'] + core_sources,
  c_args: build_args,
  dependencies: _dependencies + [cmocka_dep],
)

test('settings', test_settings)

test_layout = executable(
  'test_layout',
  ['test_layout.c'] + core_sources,
  c_args: build_args,
  dependencies: _dependencies + [cmocka_dep],
)

test('layout', test_layout)
