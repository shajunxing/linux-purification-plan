/*
Copyright 2024-2026 ShaJunXing <shajunxing@hotmail.com>

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
    #pragma warning(disable : 4996)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { puts("Please specify input and output filename"); errno = EINVAL; goto error; }
    FILE *in = fopen(argv[1], "rb");
    if (!in) {printf("Failed to open \"%s\"\n", argv[1]); goto error;}
    FILE *out = fopen(argv[2], "w");
    if (!out) {printf("Failed to open \"%s\"\n", argv[2]); goto error;}
    long n = 0;
    int c = 0;
    while ((c = fgetc(in)) != EOF) {
        if (n) fprintf(out, ",");
        fprintf(out, "%d", c);
        n++;
    }
    printf("%s -> %s: %ld bytes\n", argv[1], argv[2], n);
    fclose(out);
    fclose(in);
    return EXIT_SUCCESS;
error:
    printf("%d: %s\n", errno, strerror(errno));
    return EXIT_FAILURE;
}