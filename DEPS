# dependencies file
gclient_gn_args_file = 'build/config/gclient_args.gni'
gclient_gn_args = [
  'generate_location_tags',
]
vars = {
# Keep the Chromium default of generating location tags.
'generate_location_tags': True,
}

deps = {
  # ave module
  'base':
    'https://github.com/yoofa/base.git',
  'media':
    'https://github.com/yoofa/media.git',
  'third_party':
    'https://github.com/yoofa/ave_third_party.git',
}

recursedeps = [
  # all chromium deps in third_party
  'third_party',
]
