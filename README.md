# Vigilant C++ Logger

This is a C++ logging SDK send logs to the Vigilant logging platform.

## Installation

```bash
mkdir build
cd build
cmake ..
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
