project(
    'host-error-monitor',
    'cpp',
    version: '1.0',
    meson_version: '>=1.1.1',
    default_options: [
        'b_ndebug=if-release',
        'cpp_rtti=false',
        'cpp_std=c++20',
        'warning_level=3',
        'werror=true',
    ],
)

if (get_option('libpeci').enabled())
    add_project_arguments('-DLIBPECI', language: 'cpp')
endif

if (get_option('crashdump').enabled())
    add_project_arguments('-DCRASHDUMP', language: 'cpp')
endif

if (get_option('send-to-logger').enabled())
    add_project_arguments('-DSEND_TO_LOGGING_SERVICE', language: 'cpp')
endif

sdbusplus = dependency('sdbusplus')
gpiodcxx = dependency('libgpiodcxx', default_options: ['bindings=cxx'])

systemd = dependency('systemd', required: true)
systemd_system_unit_dir = systemd.get_variable(pkgconfig: 'systemdsystemunitdir')

boost = dependency(
    'boost',
    version: '>=1.82.0',
    required: false,
    include_type: 'system',
)
if not boost.found()
    boost = subproject('boost', required: true).get_variable('boost_dep')
    boost = boost.as_system('system')
endif
add_project_arguments(
    [
        '-DBOOST_ASIO_DISABLE_THREADS',
        '-DBOOST_ASIO_EXCEPTION_DISABLE',
        '-DBOOST_ASIO_NO_DEPRECATED',
        '-DBOOST_NO_RTTI',
        '-DBOOST_NO_TYPEID',
    ],
    language: 'cpp',
)

phosphor_dbus_interfaces = dependency('phosphor-dbus-interfaces')

bindir = get_option('prefix') + '/' + get_option('bindir')

deps = [boost, gpiodcxx, sdbusplus, phosphor_dbus_interfaces]

if (get_option('libpeci').enabled())
    peci = dependency('libpeci')
    deps += [peci]
endif

incs = include_directories('include')

executable(
    'host-error-monitor',
    'src/host_error_monitor.cpp',
    include_directories: incs,
    dependencies: deps,
    install: true,
    install_dir: bindir,
)

subdir('service_files')

if get_option('tests').allowed()
    subdir('tests')
endif
