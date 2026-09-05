# Local littlefs component - upstream littlefs-project/littlefs (v2.7, commit
# 130790f), vendored to match intechstudio/grid-fw's build.
#
# sukuwc/grid_common as published on the ESP registry REQUIRES a component
# named `littlefs` and #includes "littlefs/lfs.h"; sukuwc/grid_esp32_littlefs
# #includes bare "lfs.h". Both include dirs are exported here.
idf_component_register(
    SRCS
        "littlefs/lfs.c"
        "littlefs/lfs_util.c"
    INCLUDE_DIRS
        "."
        "littlefs"
)
