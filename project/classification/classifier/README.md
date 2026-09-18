

```text
classifier/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild
    ├── main.cpp
    └── model.espdl  # if CONFIG_CLASSIFIER_USE_EMBEDDED_MODEL=y
```


Our default configuration makes `CONFIG_CLASSIFIER_USE_EMBEDDED_MODEL=n`, then you need to store the model on the SD card:

```text
SD card
├── model.espdl
└── images/
    ├── img1.jpg
    ├── img2.jpg
    └── ...
```


### ESP-DL Build Failure Caused by Compiler Warnings

When building ESP-DL with newer GCC toolchains, the build may fail because warnings generated inside the ESP-DL managed component are treated as errors. The issue originates from the ESP-DL source code itself rather than from the application code, so adding compiler flags to `main/CMakeLists.txt` is not sufficient.

The workaround is to modify:
```cpp
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-array-bounds
    -Wno-deprecated-copy
    -Wno-strict-aliasing
    -Wno-overloaded-virtual
)
```
and add:

```cpp
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-array-bounds
    -Wno-deprecated-copy
    -Wno-strict-aliasing
    -Wno-overloaded-virtual
    -Wno-non-c-typedef-for-linkage
)
```

This change applies the warning suppression directly to the ESP-DL component, allowing the project to compile successfully with recent ESP-IDF and GCC versions. Since managed_components is regenerated whenever dependencies are updated, the modification may need to be reapplied after running idf.py reconfigure, updating dependencies, or deleting the build environment.