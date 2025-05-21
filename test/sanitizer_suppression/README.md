# Sanitizer Tests

This file introduces the steps to execute sanitizer tests, including address/memory/thread/undefined sanitizers.

## Address Sanitizer

### build commands
```
cmake  . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ADDRESS_SANITIZER=ON && make -C build
```

### execute
```
./build/WebRTCLinuxApplicationMaster
```

## Memory Sanitizer
Make sure to install clang before executing.

Ubuntu Environment:
```
sudo apt install clang
```

### build commands
```
cmake  . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_MEMORY_SANITIZER=ON && make -C build
```

### execute
```
./build/WebRTCLinuxApplicationMaster
```

## Undefined Sanitizer

### build commands
```
cmake  . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_UNDEFINED_SANITIZER=ON && make -C build
```

### execute
```
./build/WebRTCLinuxApplicationMaster
```

## Thread Sanitizer

### build commands
```
cmake  . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_THREAD_SANITIZER=ON && make -C build
```

### execute
```
sudo sysctl vm.mmap_rnd_bits=28
TSAN_OPTIONS="suppressions=/$repo/$root/test/sanitizer_suppression/thread_sanitizer_suppressions.txt" ./build/WebRTCLinuxApplicationMaster
```
