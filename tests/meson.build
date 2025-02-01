gtest_dep = dependency('gtest', main: true, disabler: true, required: false)
gmock_dep = dependency('gmock', disabler: true, required: false)
if not gtest_dep.found() or not gmock_dep.found()
    gtest_proj = import('cmake').subproject('googletest', required: false)
    if gtest_proj.found()
        gtest_dep = declare_dependency(
            dependencies: [
                dependency('threads'),
                gtest_proj.dependency('gtest'),
                gtest_proj.dependency('gtest_main'),
            ],
        )
        gmock_dep = gtest_proj.dependency('gmock')
    else
        assert(
            not get_option('tests').enabled(),
            'Googletest is required if tests are enabled',
        )
    endif
endif

unit_tests = []

if get_option('tests').enabled()
    # generate the test executable
    foreach unit_test : unit_tests
        test(
            unit_test,
            executable(
                unit_test,
                unit_test + '.cpp',
                '../src/host_error_monitor.cpp',
                cpp_args: '-DUNIT_TEST',
                include_directories: incs,
                dependencies: deps + [gtest_dep, gmock_dep],
            ),
        )
    endforeach
endif
