idf_component_register(
    SRCS
        "main.cpp"
        "access.cpp"
        "int8_classification_postprocessor.cpp"
        "classifier.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp-dl
        esp_jpeg
        fatfs
        sdmmc
        driver
)
