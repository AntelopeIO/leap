---
content_title: Build Antelope from Source
---

The shell scripts previously recommended for building the software have been removed in favor of a build process entirely driven by CMake. Those wishing to build from source are now responsible for installing the necessary dependencies. The list of dependencies and the recommended build procedure are in the [`README.md`](https://github.com/AntelopeIO/leap/blob/release/3.2/README.md) file. Instructions are also included for efficiently running the tests.

### Using DUNE
As an alternative to building from source, try [Docker Utilities for Node Execution](https://github.com/AntelopeIO/DUNE) for the easiest way to get started and for multi-platform support.

### Building From Source
You can also build and install Leap from source. Instructions for that currently live [here](https://github.com/AntelopeIO/leap/blob/release/3.2/README.md#build-and-install-from-source).

#### Building Pinned Build Binary Packages
The pinned build instructions have moved [here](https://github.com/AntelopeIO/leap/blob/release/3.2/README.md#pinned-build). You may want to look at the [prerequisites](https://github.com/AntelopeIO/leap/blob/release/3.2/README.md#prerequisites) and our warning on parallelization using the `-j` jobs flag [here](https://github.com/AntelopeIO/leap/blob/release/3.2/README.md#step-3---build) before you build.

#### Manual (non "pinned") Build Instructions
The unpinned build instructions have moved [here](https://github.com/AntelopeIO/leap/blob/release/3.2/README.md#unpinned-build). You may want to look at the [prerequisites](https://github.com/AntelopeIO/leap/blob/release/3.2/README.md#prerequisites) and our warning on parallelization using the `-j` jobs flag [here](https://github.com/AntelopeIO/leap/blob/release/3.2/README.md#step-3---build) before you build.

### Running Tests
Documentation on available test suites and how to run them has moved [here](https://github.com/AntelopeIO/leap/blob/release/3.2/README.md#test).
