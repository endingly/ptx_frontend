# Register the existing public consumer as an isolated installed-package CTest.
enable_testing()
function(ptx93_installed_cache_consumer_test)
    add_test(NAME st_immediate_installed_consumer
        COMMAND $<TARGET_FILE:memory_cache_hint_consumer>)
endfunction()
cmake_language(DEFER CALL ptx93_installed_cache_consumer_test)
