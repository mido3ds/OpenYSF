# OpenYSF
A [ysflight](https://ysflight.org/) clone, for learning and fun

# Assets
This game uses assets from original game.

1. [Download ysflight](https://ysflight.org/download/)
2. Copy all Ysflight files into `<repo>/assets`, so it ends with `<repo>/assets/aircraft` and `<repo>/assets/ground` and so on

# Build Dependencies
- cmake 3.21.0
- C/C++ compiler (MSVC/Clang/GCC)

```sh
cmake -S. -Bbuild

# either
cmake --build build --target open-ysf -j
./build/bin/Debug/open-ysf
# or call
./run
```
