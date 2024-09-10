#include <cstdint>
#include <iostream>
#include <array>
#include <string_view>

constexpr void static_write(const char * fname, const char * data){}

template<const char * data>
constexpr auto arr(){
    return data;
}


template<const char * data>
auto arr2(){ // intentinally not constexpr
    return data;
}

constexpr char data [] = {100,100,100};
constexpr std::array<char, 15> std_arr_data = {116,104,105,115,32,105,115,32,97,32,116,101,115,116,10};

int main() {
    static_write("/dev/stdout", arr<data>()); // This will write the byte array to "output.bin"
    static_write("/dev/stdout", data);
    static_write("/dev/stdout", "this is a test\n"); // This will write the byte array to "output.bin"
    static_write("/dev/stdout", std_arr_data.data());
    //static_write("/dev/stdout", arr2<data>());
    for(int i=0; i < 4; ++i){static_write("/dev/stdout", std_arr_data.data());}
    return 0;
}