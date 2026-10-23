/* gcc -static -o init init.c */

#include <stdio.h>

int main() {
    for (;;) {
        puts("Hello");
        getchar();
    }
}