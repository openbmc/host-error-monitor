project(
  'host-error-monitor',
  'cpp',
  version : '1.0',
  meson_version: '>=1.1.1',
  default_options: [
    'b_ndebug=if-release',
    'cpp_rtti=false',
    'cpp_std=c++20',
    'warning_level=3',
    'werror=true',
  ]
)

if(get_option('libpeci').enabled())
  add_project_arguments('-DLIBPECI', language : 'cpp')
endif

if(get_option('crashdump').enabled())
  add_project_arguments('-DCRASHDUMP', language : 'cpp')
endif

sdbusplus = dependency('sdbusplus')
gpiodcxx = dependency('libgpiodcxx',
    default_options: ['bindings=cxx'],
)

systemd = dependency('systemd', required: true)
systemd_system_unit_dir = systemd.get_variable(pkgconfig:
    'systemdsystemunitdir'
)

boost = dependency('boost', version: '>=1.82')
add_project_arguments(
  [
    '-DBOOST_ASIO_DISABLE_THREADS',
    '-DBOOST_ASIO_EXCEPTION_DISABLE',
    '-DBOOST_ASIO_NO_DEPRECATED',
    '-DBOOST_NO_RTTI',
    '-DBOOST_NO_TYPEID',
  ],
  language : 'cpp'
)

bindir = get_option('prefix') + '/' + get_option('bindir')

deps = [
  boost,
  gpiodcxx,
  sdbusplus,
]

if(get_option('libpeci').enabled())
  peci = dependency('libpeci')
  deps += [
    peci,
  ]
endif

executable(
  'host-error-monitor',
  'src/host_error_monitor.cpp',
  include_directories: include_directories('include'),
  dependencies: deps,
  install: true,
  install_dir: bindir
)

subdir('service_files')