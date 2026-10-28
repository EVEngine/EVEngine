# eve_raw_string_literal(<out_var> <content>)
#
# Builds a C++ raw-string literal for <content>, choosing a raw-string
# delimiter that does not occur in it. The old fixed R"(...)" form broke the
# generated C++ the day a script contained the terminator sequence )" — with a
# per-content delimiter that can never be embedded, that failure mode is gone.
#
# The delimiter grows ("eve", "evex", "evexx", ...) until the sequence
# ")<delim>\"" no longer appears in the content, then the literal is emitted as
#   R"<delim>(<content>)<delim>"
function(eve_raw_string_literal out_var content)
    set(_delim "eve")
    # Bounded scan (CMake while() triggers CMP0130 diagnostics on some versions).
    foreach(_eve_delim_attempt RANGE 0 32)
        string(FIND "${content}" ")${_delim}\"" _pos)
        if(_pos EQUAL -1)
            break()
        endif()
        string(APPEND _delim "x")
    endforeach()
    string(LENGTH "${_delim}" _eve_delim_len)
    if(_eve_delim_len GREATER 32)
        message(FATAL_ERROR
            "eve_raw_string_literal: no raw-string delimiter fits the embedded source")
    endif()
    set(${out_var} "R\"${_delim}(${content})${_delim}\"" PARENT_SCOPE)
endfunction()
