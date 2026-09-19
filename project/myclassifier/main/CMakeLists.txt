idf_component_register(
    SRCS
        "main.cpp"
        "access.cpp"
        "classifier.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp-dl
        esp_jpeg
        fatfs
        sdmmc
        driver
)
