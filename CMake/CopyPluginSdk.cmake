# Header-only SDK contents; never copy C++ implementations or another repository.
file(MAKE_DIRECTORY "${SDK_ROOT}/src" "${SDK_ROOT}/lib")
file(COPY "${SOURCE_ROOT}/src/" DESTINATION "${SDK_ROOT}/src"
    FILES_MATCHING PATTERN "*.h" PATTERN "*.inc")
file(COPY "${IMPORT_LIBRARY}" DESTINATION "${SDK_ROOT}/lib")
