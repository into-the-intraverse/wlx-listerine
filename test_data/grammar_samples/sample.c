// A simple C program
#include <stdio.h>

typedef struct {
    int x;
    float y;
} Point;

int main(int argc, char** argv) {
    const char* msg = "hello world";
    int count = 42;
    printf("%s %d\n", msg, count);
    return 0;
}
