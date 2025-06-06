JAVA_PROGRAM()

IF(JDK_VERSION == "")
    JDK_VERSION(11)
ENDIF()

PROVIDES(junit-runner)

INCLUDE(${ARCADIA_ROOT}/devtools/junit-runner/ya.make.dependency_management.inc)

PEERDIR(
    devtools/jtest
    devtools/jtest-annotations/junit4

    contrib/java/junit/junit
)

JAVA_SRCS(
    SRCDIR src/main/java **/*.java
)

LINT(base)
END()

RECURSE_FOR_TESTS(
    test-pack/test
    src/test
)
