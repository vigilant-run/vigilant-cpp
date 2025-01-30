# Vigilant C++ Logger

This is a C++ logging SDK send logs to the Vigilant logging platform.

## Dependencies
- C++17 or higher
- CMake 3.14 or higher
- libcurl
- nlohmann/json
- OpenSSL 1.1+ (required by libcurl for HTTPS)

## Installation

1. Clone the repository
```bash
git clone git@github.com:vigilant-run/vigilant-cpp.git
cd vigilant-cpp
```

2. Build the project

```bash
mkdir build
cd build
cmake ..
cmake --build .
cmake --install .
```

## Usage

```cpp
#include <vigilant/logger.h>

int main()
{
  Logger logger = LoggerBuilder()
                      .withName("cpp-test")
                      .withToken("tk_1234567890")
                      .build();

  logger.debug("This is a debug message");
  logger.info("This is an info message");
  logger.warn("This is a warning message");
  logger.error("An error occurred");

  logger.shutdown();

  return 0;
}
```
