idf_component_register(SRCS "display_settings.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash log)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
