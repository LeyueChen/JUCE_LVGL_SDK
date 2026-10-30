add_library(sdk_user STATIC
    cores/core_roles.c cores/core_cpu_0.c cores/core_cpu_1.c
    cores/core_cpu_2.c cores/core_cpu_3.c app/app.c ui/home.c)
target_link_libraries(sdk_user PUBLIC sdk::runtime)
