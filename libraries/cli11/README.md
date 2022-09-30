
## instructions for building custom leap-cli11 library

leap-cli11 interface only library created in order to simplify integration of modified version of command line parsing library cli11

to update included in this library include/cli11/CLI11.hpp file it needs to be (re)geenerated from repository containing forked/modified version of it, in particular:


```bash
git clone https://github.com/AntelopeIO/CLI11.git
cd CLI11
mkdir build
cd build
cmake -DCLI11_SINGLE_FILE=ON ..
make -j
```

Resulting single-header will be located in:

```cpp
build/include/CLI11.hpp
```

And is ready to be copied to include/cli11/CLI11.hpp of leap-cli11 library

Automated CLI11 subproject build / import of CLI11.hpp header will be added in future versions.
