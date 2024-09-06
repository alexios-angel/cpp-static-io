#include <cstdint>
#include <iostream>
#include <array>
#include <string_view>

template<typename X, typename Y>
constexpr void static_write(X fname, Y data){}

template<const char * data>
constexpr auto arr(){
    return data;
}


template<const char * data>
auto arr2(){ // intentinally not constexpr
    return data;
}

constexpr char data [] = {100,100,100};
constexpr std::array<uint8_t, 3> std_arr_data = {100,100,100};

int main() {
    static_write("/dev/stdout", arr<data>()); // This will write the byte array to "output.bin"
    static_write("/dev/stdout", data);
    static_write("/dev/stdout", std_arr_data);
    static_write("/dev/stdout", 1);
    static_write("/dev/stdout", "this is a test\n"); // This will write the byte array to "output.bin"
    static_write("/dev/stdout", arr2<data>());
    return 0;
}