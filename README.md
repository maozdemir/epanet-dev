# epanet-dev

[![Build Status](https://api.travis-ci.org/OpenWaterAnalytics/epanet-dev.svg)](https://travis-ci.org/OpenWaterAnalytics/epanet-dev)

This is a collaborative project to develop a new version of the EPANET computational engine for analyzing water distribution systems.

## Introduction

This project seeks to develop a new version of the EPANET computational engine and its associated API that includes recent advancements and improvements in water distribution system modeling. It is currently using EPANET 3 as its working title. Written in C++, it employs an object oriented approach that allows the code to be more modular, extensible, and easier to maintain.

EPANET was originally developed by the U.S. Environmental Protection Agency (USEPA) and placed in the public domain. The latest official version (2.2) can be found on the [EPANET research page](https://www.epa.gov/water-research/epanet). The new version being developed by this project represents an independent effort that is part of the [Open Source EPANET Initiative announcement](http://community.wateranalytics.org/t/announcement-of-an-open-source-epanet-initiative/117) and is neither supported nor endorsed by USEPA.

## Building EPANET 3

The source code can be compiled as both a shared library and a command-line executable. Any C++ compiler that supports the C++11 language standard can be used.

To build using CMake on Linux/Mac:

```sh
mkdir build && cd build
cmake .. && make
```

The shared library (`libepanet3.so`) will be found in the `/lib` sub-directory and the command-line executable (`run-epanet3`) will be in the `/bin` sub-directory.

To build using CMake on Windows with Visual Studio:

```sh
mkdir build && cd build
cmake -G "Visual Studio n yyyy" ..
cmake --build . --config Release
```

where `n yyyy` is the version and year of the Visual Studio release to use (e.g., 16 2019). Both the shared library (`epanet3.dll`) and the command-line executable (`run-epanet3.exe`) will be found in the `\bin\Release` sub-directory as will an `epanet3.lib` file needed to build applications that link to the library.

To build using CMake on Windows with MinGW:

```sh
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

Both the shared library (`libepanet3.dll`) and the command-line executable (`run-epanet3.exe`) will be found in the `\bin` sub-directory.

## Running EPANET 3

To run the command line executable under Linux/Mac enter the following command from a terminal window:

```sh
./run-epanet3 input.inp report.txt
```

where `input.inp` is the name of a properly formatted EPANET input file and `report.txt` is the name of a plain text file where results will be written. For Windows  , enter the following command in a Command Prompt window:

```sh
run-epanet3 input.inp report.txt
```

The EPANET 3 shared library contains an API that allows one to write custom applications by making function calls to it. A small example application can be found in the [Differences From EPANET2 document](https://github.com/OpenWaterAnalytics/epanet-dev/blob/develop/doc/Differences%20From%20EPANET2.md).

## Irrigation optimization

The command line executable now supports irrigation pipe-diameter optimization:

```sh
run-epanet3 --optimize-irrigation input.inp config.json report.txt optimized.inp
```

The optimizer also accepts CSV catalogs for backward compatibility:

```sh
run-epanet3 --optimize-irrigation input.inp catalog.csv report.txt optimized.inp
```

### JSON configuration

The preferred configuration format is JSON. See [irrigation_config.json](irrigation_config.json) for an example. Supported top-level fields are:

- `unitsBasis`: `"user"` or `"internal"`
- `options`: optimization options such as `minPressure`, `minVelocity`, `maxVelocity`, `minSegmentLength`, `maxSegments`, `allowSegmented`, and `detailedReport`
- `zone`: optional explicit irrigation-zone selectors using IDs, tags, and wildcard ID patterns
- `catalog`: an array of diameter options with `name`, `diameter`, `costPerLength`, `minVelocity`, and `maxVelocity`

#### JSON schema notes

The optimizer accepts a lightweight JSON object with this shape:

```json
{
  "unitsBasis": "user",
  "zone": {
    "nodeTags": ["IRRIGATION_ZONE"],
    "pipeIdPatterns": ["P*"],
    "excludePipeIds": ["P99"]
  },
  "options": {
    "allowSegmented": true,
    "detailedReport": true,
    "minSegmentLength": 50.0,
    "maxSegments": 3,
    "minPressure": 25.0,
    "minVelocity": 0.3,
    "maxVelocity": 2.5
  },
  "catalog": [
    {
      "name": "D200",
      "diameter": 200.0,
      "costPerLength": 72.8,
      "minVelocity": 0.3,
      "maxVelocity": 2.5
    }
  ]
}
```

Schema notes:

- `unitsBasis` controls whether the numeric values are interpreted in EPANET user units (`"user"`) or EPANET internal units (`"internal"`).
- If `unitsBasis` is omitted, the optimizer uses backward-compatible heuristics.
- `options` is optional.
- `zone` is optional. If omitted, the optimizer falls back to automatic irrigation-zone filtering.
- `zone` supports direct pipe/link selectors (`pipeIds`, `pipeIdPatterns`, `pipeTags`) and node/junction selectors (`nodeIds`, `nodeIdPatterns`, `nodeTags`), plus matching `exclude...` forms.
- ID patterns are case-insensitive and support `*` and `?` wildcards.
- Tags come from the EPANET `[TAGS]` section (`NODE id tag` or `LINK id tag`) and are preserved when saving INP files.
- `catalog` is required and must be a non-empty array.
- Each catalog item must supply numeric `diameter` and `costPerLength` values.
- `minVelocity` and `maxVelocity` are optional per-item overrides; otherwise global option values are used.
- JSON with UTF-8 BOM is accepted.

### Segmented diameter optimization

Segmented diameter optimization is performed natively inside EPANET by splitting eligible branch pipes into multiple pipe sections when pressure margins allow it. For looped or general meshed systems, segmented topology rewriting is skipped automatically and the optimizer falls back to safe whole-pipe optimization.

### Reports

The irrigation report includes:

- optimization mode per pipe (`UNIFORM` or `SEGMENTED`)
- critical junction, pressure, required pressure, and margin
- per-segment length, diameter, velocity, and unit cost when detailed reporting is enabled
- feasibility notes when the provided catalog cannot satisfy all pressure constraints
- irrigation-zone filtering notes describing how the eligible pipe set was inferred

### Irrigation-zone selection

The optimizer first looks for explicit zone selectors in the JSON `zone` object. These selectors can target:

- specific pipe/link IDs,
- node/junction IDs,
- EPANET tags from the `[TAGS]` section,
- and wildcard ID patterns.

When no explicit `zone` object is supplied, the optimizer falls back to automatic irrigation-zone filtering.

### Automatic irrigation-zone filtering

The optimizer now automatically narrows optimization to likely irrigation pipes:

- it finds demand-bearing junctions,
- traces source-to-demand paths from reservoirs and tanks when the network is branch-like,
- and limits optimization to those pipes.

If no unique subset can be inferred, all pipes remain eligible and the report states that fallback explicitly.

### Irrigation API options

The public API now includes:

- `EN_optimizeIrrigation()`
- `EN_optimizeIrrigationProject()`
- `EN_setIrrigationOption()`
- `EN_getIrrigationOption()`

Supported irrigation option enums are:

- `EN_IRR_ALLOW_SEGMENTED`
- `EN_IRR_MIN_PRESSURE`
- `EN_IRR_MIN_VELOCITY`
- `EN_IRR_MAX_VELOCITY`
- `EN_IRR_MIN_SEGMENT_LENGTH`
- `EN_IRR_MAX_SEGMENTS`
- `EN_IRR_DETAILED_REPORT`

### Small C API example

A minimal example is provided in [doc/irrigation_api_example.c](doc/irrigation_api_example.c). It shows how to:

- create a project,
- set irrigation optimization options,
- read back configured values,
- and run irrigation optimization from C.

### Small C++ API example

A matching C++ sample is provided in [doc/irrigation_api_example.cpp](doc/irrigation_api_example.cpp).

### Sample build targets

The CMake build now produces these sample executables alongside `run-epanet3`:

- `irrigation_api_example_c`
- `irrigation_api_example_cpp`

Both samples accept optional command line arguments:

1. input INP file
2. irrigation config JSON file
3. report file
4. optimized output INP file

## API Reference

The EPANET 3 API has a similar flavor to that of EPANET 2, but all of the functions have been re-named and require that an EPANET project first be created and included as an argument in all function calls. (This makes the API capable of analyzing several projects in parallel in a thread safe manner.) EPANET 3 is able to read EPANET 2 input files but uses a different layout for its binary results file. Thus it will not be compatible with the current EPANET 2 GUI. Details of the API, including the changes and additions made to various computational components of EPANET, can be found in the 'docs' section of this repository.

You can access the full documentation at [wateranalytics.org/epanet-dev](http://wateranalytics.org/epanet-dev).

## Disclaimer

This project is still in its early developmental stage. Its robustness and the accuracy of its numerical results have not been thoroughly tested. Therefore it should not yet be used as a replacement EPANET 2 nor be used in any production code for specialized applications.

## License

The new version of EPANET will be distributed under the MIT license as described in the LICENSE file of this repository.
