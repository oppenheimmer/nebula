cmake_minimum_required(VERSION 3.22)

# A deliberately invalid platform makes accidental QApplication initialization
# fail, even on a developer's machine with a working display.
set(ENV{QT_QPA_PLATFORM} nebula-no-display)
foreach(option --version -v)
    foreach(tool GUI WORKSPACE)
        execute_process(COMMAND "${NEBULA_${tool}}" "${option}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
        if(tool STREQUAL "GUI")
            set(expected "Nebula ${NEBULA_VERSION}\n")
        else()
            set(expected "nebula_workspace ${NEBULA_VERSION}\n")
        endif()
        if(NOT status STREQUAL "0" OR NOT output STREQUAL expected OR NOT error STREQUAL "")
            message(FATAL_ERROR "${tool} ${option}: status=${status}, output=${output}, error=${error}")
        endif()
    endforeach()
endforeach()

foreach(option --help -h)
    execute_process(COMMAND "${NEBULA_WORKSPACE}" "${option}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
    if(NOT status STREQUAL "0" OR NOT output MATCHES "Usage: nebula_workspace" OR NOT error STREQUAL "")
        message(FATAL_ERROR "Workspace help failed: ${status}, ${output}, ${error}")
    endif()
endforeach()

foreach(option "" backup unknown)
    execute_process(COMMAND "${NEBULA_WORKSPACE}" ${option}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
    if(NOT status STREQUAL "2" OR NOT output STREQUAL "" OR NOT error MATCHES "Usage: nebula_workspace")
        message(FATAL_ERROR "Invalid workspace command '${option}': ${status}, ${output}, ${error}")
    endif()
endforeach()

# Normal parser handling must report the same version before opening a workspace.
set(ENV{QT_QPA_PLATFORM} offscreen)
set(folder "${NEBULA_TEST_TMPDIR}/command-line-workspace")
if(EXISTS "${folder}")
    message(FATAL_ERROR "Command-line test folder already exists: ${folder}")
endif()
execute_process(COMMAND "${NEBULA_GUI}" --version --workspace "${folder}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
if(NOT status STREQUAL "0" OR NOT output STREQUAL "Nebula ${NEBULA_VERSION}\n" OR EXISTS "${folder}")
    message(FATAL_ERROR "Version command opened a workspace or failed: ${status}, ${output}, ${error}")
endif()
