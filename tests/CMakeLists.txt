add_executable(ring_buffer_test ./RingBufferTest.c ../src/common/RingBuffer.c ../src/util/Mutex.c)
target_link_libraries(ring_buffer_test)
target_compile_definitions(ring_buffer_test PRIVATE NO_LOGGING)

add_test(NAME ring_buffer_test COMMAND $<TARGET_FILE:ring_buffer_test>)

# The data readers (tables, DC6, DCC, DS1, DT1) and the network's item messages, on bytes made by the test itself
# (no game files): known input read right, broken and random input never crashing
add_executable(readers_test
        ./ReadersTest.c ./TestStubs.c
        ../src/data/DataTable.c
        ../src/types/DC6.c ../src/types/DC6Frame.c ../src/types/DCC.c ../src/types/DS1.c ../src/types/DT1.c
        ../src/net/Protocol.c ../src/net/NetMessage.c)
if (NOT MSVC)
    target_link_libraries(readers_test m)
endif ()
add_test(NAME readers_test COMMAND $<TARGET_FILE:readers_test>)
