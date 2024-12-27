# dependencies file

gclient_gn_args_file = 'build/config/gclient_args.gni'
gclient_gn_args = [
  'generate_location_tags',
]

vars = {
  # By default, we should check out everything needed to run on the main
  # chromium waterfalls. More info at: crbug.com/570091.
  'checkout_configuration': 'default',
  'checkout_instrumented_libraries': 'checkout_linux and checkout_configuration == "default"',
  'chromium_revision': '4559b6b576fc5bd8f36ad7cde13bcf5215bec9dc',

  # Keep the Chromium default of generating location tags.
  'generate_location_tags': True,

  # reclient CIPD package version
  'reclient_version': 're_client_version:0.113.0.8b45b89-gomaip',

  'buildtools_gn_version': 'git_revision:b2afae122eeb6ce09c52d63f67dc53fc517dbdc8',

  # ninja CIPD package version
  # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
  'ninja_version': 'version:2@1.11.1.chromium.6',
}

deps = {
  # ave module
  'base':
    'https://github.com/yoofa/base.git',
  'media':
    'https://github.com/yoofa/media_common.git',
  'third_party':
    'https://github.com/yoofa/ave_third_party.git',

  'build':
    'https://chromium.googlesource.com/chromium/src/build@5bce81deee6d30ac58c45f6cd53e859c62780687',
  'buildtools':
    'https://chromium.googlesource.com/chromium/src/buildtools@94d7b86a83537f8a7db7dccb0bf885739f7a81aa',
  'buildtools/linux64': {
    'packages': [
      {
        'package': 'gn/gn/linux-${{arch}}',
        'version': Var('buildtools_gn_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'checkout_linux',
  },
  'buildtools/mac': {
    'packages': [
      {
        'package': 'gn/gn/mac-${{arch}}',
        'version': Var('buildtools_gn_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'checkout_mac',
  },
  'buildtools/win': {
    'packages': [
      {
        'package': 'gn/gn/windows-amd64',
        'version': 'git_revision:',
      }
    ],
    'dep_type': 'cipd',
    'condition': 'checkout_win',
  },
  'buildtools/reclient': {
    'packages': [
      {
         # https://chrome-infra-packages.appspot.com/p/infra/rbe/client/
        'package': 'infra/rbe/client/${{platform}}',
        'version': Var('reclient_version'),
      }
    ],
    'dep_type': 'cipd',
    # Reclient doesn't have linux-arm64 package.
    'condition': 'not (host_os == "linux" and host_cpu == "arm64")',
  },

  'third_party/clang-format/script':
    'https://chromium.googlesource.com/external/github.com/llvm/llvm-project/clang/tools/clang-format.git@e5337933f2951cacd3aeacd238ce4578163ca0b9',
  'third_party/libc++/src':
    'https://chromium.googlesource.com/external/github.com/llvm/llvm-project/libcxx.git@09b99fd8ab300c93ff7b8df6688cafb27bd3db28',
  'third_party/libc++abi/src':
    'https://chromium.googlesource.com/external/github.com/llvm/llvm-project/libcxxabi.git@bac941ca447afdea824073f3138ef49869e675db',
  'third_party/libunwind/src':
    'https://chromium.googlesource.com/external/github.com/llvm/llvm-project/libunwind.git@43e5a34c5b7066a7ee15c74f09dc37b4b9b5630e',

  'third_party/ninja': {
    'packages': [
      {
        'package': 'infra/3pp/tools/ninja/${{platform}}',
        'version': Var('ninja_version'),
      }
    ],
    'dep_type': 'cipd',
  },

  'testing':
    'https://chromium.googlesource.com/chromium/src/testing@074ff55a07fb690e769eda906375f8e7cb6b39b8',
  'third_party/depot_tools':
    'https://chromium.googlesource.com/chromium/tools/depot_tools.git@f1c7c96958b849668e62799e74b204c9fe9fe17c',

  'third_party/googletest/src':
    'https://chromium.googlesource.com/external/github.com/google/googletest.git@af29db7ec28d6df1c7f0f745186884091e602e07',
  'third_party/google_benchmark/src': {
    'url': 'https://chromium.googlesource.com/external/github.com/google/benchmark.git@344117638c8ff7e239044fd0fa7085839fc03021',
  },

  ### android
  'third_party/jdk/current': {
      'packages': [
          {
              'package': 'chromium/third_party/jdk',
              'version': 'BXZwbslDFpYhPRuG8hBh2z7ApP36ZG-ZfkBWrkpnPl4C',
          },
     ],
      'condition': 'host_os == "linux" and checkout_android',
      'dep_type': 'cipd',
  },
  'third_party/android_sdk/public': {
      'packages': [
          {
              'package': 'chromium/third_party/android_sdk/public/build-tools/34.0.0',
              'version': 'YK9Rzw3fDzMHVzatNN6VlyoD_81amLZpN1AbmkdOd6AC',
          },
          {
              'package': 'chromium/third_party/android_sdk/public/emulator',
              'version': '9lGp8nTUCRRWGMnI_96HcKfzjnxEJKUcfvfwmA3wXNkC',
          },
          {
              'package': 'chromium/third_party/android_sdk/public/platform-tools',
              'version': 'HWVsGs2HCKgSVv41FsOcsfJbNcB0UFiNrF6Tc4yRArYC',
          },
          {
              'package': 'chromium/third_party/android_sdk/public/platforms/android-34',
              'version': 'u-bhWbTME6u-DjypTgr3ZikCyeAeU6txkR9ET6Uudc8C',
          },
          {
              'package': 'chromium/third_party/android_sdk/public/cmdline-tools',
              'version': 'mU9jm4LkManzjSzRquV1UIA7fHBZ2pK7NtbCXxoVnVUC',
          },
      ],
      'condition': 'checkout_android',
      'dep_type': 'cipd',
  },

  'third_party/android_toolchain/ndk': {
    'packages': [
      {
        'package': 'chromium/third_party/android_toolchain/android_toolchain',
        'version': 'wpJvg81kuXdMM66r_l9Doa-pLfR6S26Jd1x40LpwWEoC',
      },
    ],
    'condition': 'checkout_android',
    'dep_type': 'cipd',
  },

  'third_party/androidx': {
    'packages': [
      {
          'package': 'chromium/third_party/androidx',
          'version': '-zomVY2T8V3NRjAUbZNAZpFp8dAPuNdu3oQsnmhhIHEC',
      },
    ],
    'condition': 'checkout_android',
    'dep_type': 'cipd',
  },
  

  #
  'tools/clang/dsymutil': {
    'packages': [
      {
        'package': 'chromium/llvm-build-tools/dsymutil',
        'version': 'M56jPzDv1620Rnm__jTMYS62Zi8rxHVq7yw0qeBFEgkC',
      }
    ],
    'condition': 'checkout_mac',
    'dep_type': 'cipd',
  },

  'third_party/catapult':
    'https://chromium.googlesource.com/catapult.git@4f81c1e295978227d83f1b42ceff40b4f9b5b08c',

  'tools':
    'https://chromium.googlesource.com/chromium/src/tools@a8fe86b922a84de686c3b15c87e2a9ac84d06db3',
  'tools/swarming_client':
    'https://chromium.googlesource.com/infra/luci/client-py.git@d46ea7635f2911208268170512cb611412488fd8',
  'third_party/libjpeg_turbo':
    'https://chromium.googlesource.com/chromium/deps/libjpeg_turbo.git@ff19e5b2e176c61d552f68768e0e051867745321',

  # used by ffmpeg
  'third_party/nasm': {
      'url': 'https://chromium.googlesource.com/chromium/deps/nasm.git@f477acb1049f5e043904b87b825c5915084a9a29'
  },

  'third_party/ffmpeg':
    'https://chromium.googlesource.com/chromium/third_party/ffmpeg.git@d941d9677bb4802f01750fd908ec284fb72c84df',
  'third_party/libyuv':
    'https://chromium.googlesource.com/libyuv/libyuv.git@49ebc996aa8c4bdf89c1b5ea461eb677234c61cc',
}

hooks = [
  {
    # Ensure that the DEPS'd "depot_tools" has its self-update capability
    # disabled.
    'name': 'disable_depot_tools_selfupdate',
    'pattern': '.',
    'action': [
        'python',
        'third_party/depot_tools/update_depot_tools_toggle.py',
        '--disable',
    ],
  },
  {
    'name': 'sysroot_arm',
    'pattern': '.',
    'condition': 'checkout_linux and checkout_arm',
    'action': ['python', 'build/linux/sysroot_scripts/install-sysroot.py',
               '--arch=arm'],
  },
  {
    'name': 'sysroot_arm64',
    'pattern': '.',
    'condition': 'checkout_linux and checkout_arm64',
    'action': ['python', 'build/linux/sysroot_scripts/install-sysroot.py',
               '--arch=arm64'],
  },
  {
    'name': 'sysroot_x86',
    'pattern': '.',
    'condition': 'checkout_linux and (checkout_x86 or checkout_x64)',
    # TODO(mbonadei): change to --arch=x86.
    'action': ['python', 'build/linux/sysroot_scripts/install-sysroot.py',
               '--arch=i386'],
  },
  {
    'name': 'sysroot_mips',
    'pattern': '.',
    'condition': 'checkout_linux and checkout_mips',
    # TODO(mbonadei): change to --arch=mips.
    'action': ['python', 'build/linux/sysroot_scripts/install-sysroot.py',
               '--arch=mipsel'],
  },
  {
    'name': 'sysroot_x64',
    'pattern': '.',
    'condition': 'checkout_linux and checkout_x64',
    # TODO(mbonadei): change to --arch=x64.
    'action': ['python', 'build/linux/sysroot_scripts/install-sysroot.py',
               '--arch=amd64'],
  },
  {
    # Case-insensitivity for the Win SDK. Must run before win_toolchain below.
    'name': 'ciopfs_linux',
    'pattern': '.',
    'condition': 'checkout_win and host_os == "linux"',
    'action': [ 'python',
                'third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-browser-clang/ciopfs',
                '-s', 'build/ciopfs.sha1',
    ]
  },
  {
    # Update the Windows toolchain if necessary. Must run before 'clang' below.
    'name': 'win_toolchain',
    'pattern': '.',
    'condition': 'checkout_win',
    'action': ['python', 'build/vs_toolchain.py', 'update', '--force'],
  },
  {
    # Update the Mac toolchain if necessary.
    'name': 'mac_toolchain',
    'pattern': '.',
    'condition': 'checkout_mac',
    'action': ['python', 'build/mac_toolchain.py'],
  },
  {
    # Note: On Win, this should run after win_toolchain, as it may use it.
    'name': 'clang',
    'pattern': '.',
    'action': ['python', 'tools/clang/scripts/update.py'],
  },
  # Pull clang-format binaries using checked-in hashes.
  #{
  #  'name': 'clang_format_win',
  #  'pattern': '.',
  #  'condition': 'host_os == "win"',
  #  'action': [ 'download_from_google_storage',
  #              '--no_resume',
  #              '--platform=win32',
  #              '--no_auth',
  #              '--bucket', 'chromium-clang-format',
  #              '-s', 'buildtools/win/clang-format.exe.sha1',
  #  ],
  #},
  #{
  #  'name': 'clang_format_mac',
  #  'pattern': '.',
  #  'condition': 'host_os == "mac"',
  #  'action': [ 'download_from_google_storage',
  #              '--no_resume',
  #              '--platform=darwin',
  #              '--no_auth',
  #              '--bucket', 'chromium-clang-format',
  #              '-s', 'buildtools/mac/clang-format.sha1',
  #  ],
  #},
  #{
  #  'name': 'clang_format_linux',
  #  'pattern': '.',
  #  'condition': 'host_os == "linux"',
  #  'action': [ 'download_from_google_storage',
  #              '--no_resume',
  #              '--platform=linux*',
  #              '--no_auth',
  #              '--bucket', 'chromium-clang-format',
  #              '-s', 'buildtools/linux64/clang-format.sha1',
  #  ],
  #},
  {
    # Update LASTCHANGE.
    'name': 'lastchange',
    'pattern': '.',
    'action': ['python', 'build/util/lastchange.py',
               '-o', 'build/util/LASTCHANGE'],
  },
]
