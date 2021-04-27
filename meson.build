project(
    'hostpwrctl',
    'cpp',
    default_options: [
        'warning_level=3',
        'werror=true',
        'cpp_std=c++17',
    ],
    license: 'Apache-2.0',
)

executable(
    'hostpwrctl',
    [
        'hostpwrctl.cpp',
    ],
    dependencies: [
        dependency('sdbusplus'),
        dependency('sdeventplus'),
    ],
    install: true,
    install_dir: get_option('sbindir'),
)
